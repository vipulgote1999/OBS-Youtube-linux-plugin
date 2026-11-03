#include "libsupertonic-api.hpp"

#include <memory>

/**
 * Compile-only guard: the PIMPL header must not require ONNX Runtime headers
 * from any translation unit that merely names the type.
 */
int main()
{
    std::unique_ptr<SupertonicApi> api;
    return api ? 1 : 0;
}
