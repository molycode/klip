#include "json/syntax_error.hpp"

#include "syntax_error_handler.hpp"

namespace Klip::Json
{
namespace
{
constexpr bool Strict{ true };
} // namespace

//////////////////////////////////////////////////////////////////////////
std::string DescribeSyntaxError(std::string_view text, bool ignoreComments)
{
	CSyntaxErrorHandler handler{};

	CSyntaxErrorHandler::JsonValue::sax_parse(text, &handler, CSyntaxErrorHandler::JsonValue::input_format_t::json,
	                                          Strict, ignoreComments);

	return handler.GetError();
}
} // namespace Klip::Json
