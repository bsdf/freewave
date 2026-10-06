# Always fetch our own single-precision build. A system kissfft may be built for
# a different datatype, and its config package raises a hard error when asked for
# the float component, so we don't probe for it.
if(NOT TARGET kissfft::kissfft)
    # Packaging builds have no network, so kissfft comes from the platform. The
    # float component is asserted because the datatype hazard above is a real
    # correctness trap. Linkage is not a correctness matter but must still be
    # stated: with neither SHARED nor STATIC requested the config package selects
    # targets by BUILD_SHARED_LIBS, and freewave builds its own libraries static,
    # so it would look for a static kissfft and miss Debian's shared-only
    # libkissfft-float. Overridable because the choice belongs to the packager,
    # and the config package raises a fatal error rather than failing softly, so
    # it cannot be probed for.
    if(FREEWAVE_USE_SYSTEM_DEPS)
        set(FREEWAVE_KISSFFT_LINKAGE "SHARED" CACHE STRING
            "Linkage of the system kissfft to link against (SHARED or STATIC)")
        find_package(kissfft CONFIG REQUIRED COMPONENTS float ${FREEWAVE_KISSFFT_LINKAGE})
        return()
    endif()

    include(FetchContent)

    # Single-precision, library only — no test/tool executables, no pkg-config.
    set(KISSFFT_DATATYPE "float" CACHE INTERNAL "")
    set(KISSFFT_STATIC ON CACHE INTERNAL "")
    set(KISSFFT_TEST OFF CACHE INTERNAL "")
    set(KISSFFT_TOOLS OFF CACHE INTERNAL "")
    set(KISSFFT_PKGCONFIG OFF CACHE INTERNAL "")
    set(KISSFFT_OPENMP OFF CACHE INTERNAL "")

    FetchContent_Declare(kissfft
      GIT_REPOSITORY https://github.com/mborgerding/kissfft.git
      GIT_TAG        131.1.0
      GIT_SHALLOW    TRUE
    )

    FetchContent_MakeAvailable(kissfft)
endif()
