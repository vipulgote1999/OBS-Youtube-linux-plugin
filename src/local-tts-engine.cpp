#include "local-tts-engine.hpp"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>
#include <QUrl>

#include <algorithm>
#include <utility>

#include "audio-resampler.hpp"
#include "wav.hpp"

namespace {
constexpr int kOutputRate = 22050;
constexpr int kConnectTimeoutMs = 3000;
constexpr int kResponseTimeoutMs = 20000;

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QByteArray decodeChunked(const QByteArray &body, QString *error)
{
    QByteArray output;
    int position = 0;
    while (position < body.size()) {
        const int lineEnd = body.indexOf("\r\n", position);
        if (lineEnd < 0)
            break;

        bool ok = false;
        const int size = body.mid(position, lineEnd - position).toInt(&ok, 16);
        if (!ok || size < 0) {
            fail(error, QStringLiteral("malformed chunked HTTP response"));
            return {};
        }
        position = lineEnd + 2;
        if (size == 0)
            break;
        if (position + size > body.size()) {
            fail(error, QStringLiteral("truncated chunked HTTP response"));
            return {};
        }
        output.append(body.mid(position, size));
        position += size + 2; // skip the chunk's trailing CRLF
    }
    return output;
}

bool responseComplete(const QByteArray &raw)
{
    const int headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return false;
    const QByteArray head = raw.left(headerEnd).toLower();
    if (head.contains("transfer-encoding: chunked"))
        return raw.endsWith("0\r\n\r\n");

    const int index = head.indexOf("content-length:");
    if (index < 0)
        return false; // no length: the server must close the connection
    const int end = head.indexOf("\r\n", index);
    const int length = head.mid(index + 15, end - index - 15).trimmed().toInt();
    return raw.size() >= headerEnd + 4 + length;
}
} // namespace

LocalTtsEngine::LocalTtsEngine(QString url, QString voice, QString model)
    : url_(std::move(url)), voice_(std::move(voice)), model_(std::move(model))
{
}

bool LocalTtsEngine::synthesize(const QString &text, std::vector<float> *pcm, int *sampleRate,
                                QString *error)
{
    if (error)
        error->clear();
    if (!pcm || !sampleRate)
        return fail(error, QStringLiteral("invalid TTS output buffer"));
    pcm->clear();
    *sampleRate = 0;

    if (cancelRequested_.load())
        return fail(error, QStringLiteral("cancelled"));

    QJsonObject request;
    request.insert(QStringLiteral("model"), model_);
    request.insert(QStringLiteral("input"), text);
    request.insert(QStringLiteral("voice"), voice_);
    request.insert(QStringLiteral("response_format"), QStringLiteral("wav"));

    QByteArray response;
    if (!post(QJsonDocument(request).toJson(QJsonDocument::Compact), &response, error))
        return false;

    int rate = 0;
    if (!parseWavToMono(response, pcm, &rate, error))
        return false;

    if (rate != kOutputRate) {
        *pcm = AudioResampler::resample(*pcm, rate, kOutputRate);
        rate = kOutputRate;
    }

    if (pcm->empty())
        return fail(error, QStringLiteral("local TTS server returned no audio"));

    *sampleRate = rate;
    return true;
}

bool LocalTtsEngine::post(const QByteArray &body, QByteArray *response, QString *error)
{
    const QUrl url(url_);
    if (!url.isValid() || url.scheme() != QLatin1String("http")) {
        return fail(error, QStringLiteral("local TTS URL must be an http:// URL, got '%1'")
                               .arg(url_));
    }
    const QString host = url.host();
    const int port = url.port(80);
    const QString path = url.path().isEmpty() ? QStringLiteral("/v1/audio/speech") : url.path();

    QTcpSocket socket;
    socket.connectToHost(host, quint16(port));
    if (!socket.waitForConnected(kConnectTimeoutMs)) {
        return fail(error, QStringLiteral("local TTS server is not reachable at %1: %2")
                               .arg(url_, socket.errorString()));
    }

    const QByteArray hostHeader =
        (port == 80 ? host : QStringLiteral("%1:%2").arg(host).arg(port)).toUtf8();
    QByteArray request;
    request += "POST " + path.toUtf8() + " HTTP/1.1\r\n";
    request += "Host: " + hostHeader + "\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Accept: audio/wav\r\n";
    request += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    request += "Connection: close\r\n\r\n";
    request += body;

    if (socket.write(request) != request.size() || !socket.waitForBytesWritten(kConnectTimeoutMs)) {
        return fail(error, QStringLiteral("could not send the TTS request: %1")
                               .arg(socket.errorString()));
    }

    QElapsedTimer timer;
    timer.start();
    QByteArray raw;
    while (true) {
        if (cancelRequested_.load()) {
            socket.abort();
            return fail(error, QStringLiteral("cancelled"));
        }
        if (timer.elapsed() > kResponseTimeoutMs) {
            socket.abort();
            return fail(error, QStringLiteral("local TTS server timed out after %1 s")
                                   .arg(kResponseTimeoutMs / 1000));
        }
        if (socket.waitForReadyRead(100))
            raw += socket.readAll();
        else if (socket.state() == QAbstractSocket::UnconnectedState)
            break;
        if (responseComplete(raw))
            break;
    }
    raw += socket.readAll();

    const int headerEnd = raw.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return fail(error, QStringLiteral("local TTS server returned a malformed HTTP response"));

    const QByteArray head = raw.left(headerEnd);
    const QByteArray payload = raw.mid(headerEnd + 4);
    const QList<QByteArray> lines = head.split('\n');
    if (lines.isEmpty())
        return fail(error, QStringLiteral("local TTS server returned an empty HTTP response"));

    const QList<QByteArray> statusParts = lines.first().trimmed().split(' ');
    bool ok = false;
    const int status = statusParts.size() >= 2 ? statusParts.at(1).toInt(&ok) : 0;
    if (!ok || status < 100)
        return fail(error, QStringLiteral("local TTS server returned an invalid status line"));

    bool chunked = false;
    int contentLength = -1;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "transfer-encoding" && value.toLower().contains("chunked"))
            chunked = true;
        else if (name == "content-length")
            contentLength = value.toInt();
    }

    QByteArray bodyBytes;
    if (chunked) {
        bodyBytes = decodeChunked(payload, error);
        if (bodyBytes.isEmpty() && error && !error->isEmpty())
            return false;
    } else if (contentLength >= 0 && contentLength <= payload.size()) {
        bodyBytes = payload.left(contentLength);
    } else {
        bodyBytes = payload;
    }

    if (status != 200) {
        const QString detail = QString::fromUtf8(bodyBytes.left(200)).trimmed();
        return fail(error, QStringLiteral("local TTS server returned HTTP %1%2")
                               .arg(status)
                               .arg(detail.isEmpty() ? QString()
                                                     : QStringLiteral(": ") + detail));
    }
    if (bodyBytes.isEmpty())
        return fail(error, QStringLiteral("local TTS server returned an empty body"));

    *response = bodyBytes;
    return true;
}

void LocalTtsEngine::cancel()
{
    cancelRequested_.store(true);
}

void LocalTtsEngine::resetCancel()
{
    cancelRequested_.store(false);
}
