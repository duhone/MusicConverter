#pragma once
import CR.Engine.Core.Log;

#define CR_ERROR(fmtString, ...)                                                                   \
	do {                                                                                             \
		CR::Engine::Core::Log::Error(std::source_location::current(),                                  \
		                             fmtString __VA_OPT__(, ) __VA_ARGS__);                            \
	} while(false)

#if CR_DEBUG || CR_RELEASE
#define CR_WARN(fmtString, ...)                                                                    \
	do {                                                                                             \
		CR::Engine::Core::Log::Error(std::source_location::current(),                                  \
		                             fmtString __VA_OPT__(, ) __VA_ARGS__);                            \
	} while(false)
#else
#define CR_WARN(fmtString, ...)                                                                    \
	do {                                                                                             \
		CR::Engine::Core::Log::Warn(std::source_location::current(),                                   \
		                            fmtString __VA_OPT__(, ) __VA_ARGS__);                             \
	} while(false)
#endif

#if CR_DEBUG || CR_RELEASE
#define CR_LOG(fmtString, ...)                                                                     \
	do {                                                                                             \
		CR::Engine::Core::Log::Info(std::source_location::current(),                                   \
		                            fmtString __VA_OPT__(, ) __VA_ARGS__);                             \
	} while(false)
#else
#define CR_LOG(fmtString, ...)
#endif

#if CR_DEBUG || CR_RELEASE

#define CR_ASSERT_AUDIT(condition, fmtString, ...)                                                 \
	do {                                                                                             \
		if(!(condition)) { CR_ERROR(fmtString __VA_OPT__(, ) __VA_ARGS__); }                           \
	} while(false);                                                                                  \
	__assume(!(condition));

#else

#define CR_ASSERT_AUDIT(condition, fmtString, ...) __assume(!(condition));

#endif

#define CR_REQUIRES_AUDIT(condition, fmtString, ...)                                               \
	CR_ASSERT_AUDIT(condition, fmtString __VA_OPT__(, ) __VA_ARGS__);

#define CR_ENSURES_AUDIT(condition, fmtString, ...)                                                \
	CR_ASSERT_AUDIT(condition, fmtString __VA_OPT__(, ) __VA_ARGS__);

#define CR_ASSERT(condition, fmtString, ...)                                                       \
	do {                                                                                             \
		if(!(condition)) { CR_ERROR(fmtString __VA_OPT__(, ) __VA_ARGS__); }                           \
	} while(false);                                                                                  \
	//__assume(!(condition));

#define CR_REQUIRES(condition, fmtString, ...)                                                     \
	CR_ASSERT(condition, fmtString __VA_OPT__(, ) __VA_ARGS__);

#define CR_ENSURES(condition, fmtString, ...)                                                      \
	CR_ASSERT(condition, fmtString __VA_OPT__(, ) __VA_ARGS__);
