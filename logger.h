#pragma once

#include <vector>
#include <ostream>
#include <cstring>
#include <cassert>
#include <type_traits>
#include <string>

namespace Logger {
	
// == Public interface ==
	
// Will cosume all stored log data and render it to std::ostream
void Consume();
	

namespace Detail {

using FormatFunction = void(*)(std::ostream& stream, const char* format, const char* buffer);


// helper
template <typename... Types>
constexpr unsigned sizeof_args(Types&&...) { return sizeof...(Types); }

// helper
constexpr size_t CountPlaceholders(const char* formatString)
{
	return (formatString[0] == '\0') ? 0 : (
		(formatString[0] == '%' ? 1u : 0u) + CountPlaceholders(formatString + 1));
}

// === GetArgsSize ===

// base case
static inline size_t GetArgsSize() { return 0; }

// size of a single argument (trivially-copyable version)
template<typename Arg>
static size_t GetArgSize(const Arg& arg)
{
	static_assert(std::is_trivially_copyable<Arg>::value, "this function is supposed to be used only with trivially-copyable types");
	return sizeof(arg);
}

static inline size_t GetArgSize(const char* str) { return std::strlen(str) + 1; }
static inline size_t GetArgSize(const std::string& s) { return s.length() + 1; }


// process a list of arguments
template<typename Arg, typename... Args>
static size_t GetArgsSize(const Arg& arg, const Args... args)
{
	return GetArgSize(arg) + GetArgsSize(args...);
}

// === CopyArgs ===

// Note: we _must_ have an option for non-trivially copyable args (SFINAE)
template <typename T>
static char* CopyArg(char* argsData, T arg)
{
	static_assert(std::is_trivially_copyable<T>::value, "this function is supposed to be used only with trivially-copyable types");
	//std::memcpy(argsData, &arg, sizeof(arg));
	*reinterpret_cast<T*>(argsData) = arg;
	return argsData + sizeof(arg);
}

static inline char* CopyArg(char* argsData, const char* str)
{
	std::strcpy(argsData, str);
	return argsData + std::strlen(str) + 1; // suboptimal
}
static inline char* CopyArg(char* argsData, const std::string& s)
{
	return CopyArg(argsData, s.c_str());
}

// specializations

// base case (terminator)
inline char* CopyArgs(char* argsData) { return argsData; }

// write a single arg to the buffer and continue with the tail
template<typename Arg, typename ... Args>
static char* CopyArgs(char* argsData, const Arg& arg, const Args&... args)
{
	argsData = CopyArg(argsData, arg);
	return CopyArgs(argsData, args...);
}

// === Formatting ===

// terminator
template<typename... Args>
static std::enable_if_t<sizeof...(Args)==0> FormatArgs(std::ostream& s, const char* format, const char* /*buffer*/)
{
	s << format << std::endl;
}

// TODO specialize for other types
template<typename Arg>
static const char* FormatArg(std::ostream& outputStream, const char* argsData)
{
	const Arg* arg = reinterpret_cast<const Arg*>(argsData);
	outputStream << *arg;
	return argsData + sizeof(Arg);
}

template<>
inline const char* FormatArg<const char*>(std::ostream& outputStream, const char* argsData)
{
	auto l = std::strlen(argsData);
	outputStream.write(argsData, l);
	return argsData + l + 1;
}

template<>
inline const char* FormatArg<std::string>(std::ostream& outputStream, const char* argsData)
{
	return FormatArg<const char*>(outputStream, argsData);
}

template<typename Arg, typename... Args>
static void FormatArgs(std::ostream& s, const char* format, const char* buffer)
{
	const char* placeholder = std::strstr(format, "%");
	s.write(format, placeholder-format);
	FormatArgs<Args...>(s, placeholder+1,  FormatArg<Arg>(s, buffer));
}


// === Log buffer managemebnt and protocol ==

// Obtains continous buffer to store log args in the fatstest possible way
char* GetWriteBuffer(int size);

// Return buffer to the next message, nullptr if none
char* GetNextReadBuffer();

struct Header
{
	size_t ArgsSize;
	const char* FormatString;
	FormatFunction Formatter;
};

// === Writing to buffer ===

// write the given log line to a buffer
template <typename... Args>
static void WriteLog(const char* formatString, const Args&... args)
{
	// calculate space needed in buffer
	size_t argsSize = GetArgsSize<Args...>(args...);

	// get buffer
	char* data = GetWriteBuffer(argsSize + sizeof(Header));
	Header* header = reinterpret_cast<Header*>(data);
	char* argsBuffer = data + sizeof(Header);

	// store crucial info
	header->ArgsSize = argsSize;
	header->FormatString = formatString;
	header->Formatter = FormatArgs<Args...>; // TODO

	// copy args in the buffer
	char* end = CopyArgs(argsBuffer, args...);
	assert(end-argsBuffer == argsSize);
}

} // ns detail

} // ns Logger

#define LOG(formatString, ...) \
	static_assert(Logger::Detail::CountPlaceholders(formatString) == Logger::Detail::sizeof_args(__VA_ARGS__), "Number of arguments mismatch"); \
	do { \
		Logger::Detail::WriteLog(formatString, ##__VA_ARGS__); \
	} while (false);
