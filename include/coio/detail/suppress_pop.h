// ReSharper disable once CppMissingIncludeGuard

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#ifdef COIO_SUPPRESS_PUSH
#undef COIO_SUPPRESS_PUSH
#else
#error "<coio/detail/suppress_pop.h> shall be included before <coio/detail/suppress_push.h> is included again"
#endif
