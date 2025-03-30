#pragma once

#ifdef __cplusplus
    #include <complex>
    typedef std::complex<float> cfloat;
#else
    typedef float _Complex cfloat;
#endif
