#pragma once

#include "json/json.hpp"

#include <tge/non_copyable.hpp>

#include <cstddef>
#include <string>

namespace Klip::Json
{
// A SAX handler that builds nothing and keeps the parser's error; nlohmann's SAX interface names its methods.
class CSyntaxErrorHandler final : private Tge::SNoCopyNoMove
{
public:

	CSyntaxErrorHandler() = default;
	~CSyntaxErrorHandler() = default;

	using JsonValue = nlohmann::ordered_json;

	bool null();
	bool boolean(bool);
	bool number_integer(JsonValue::number_integer_t);
	bool number_unsigned(JsonValue::number_unsigned_t);
	bool number_float(JsonValue::number_float_t, JsonValue::string_t const&);
	bool string(JsonValue::string_t&);
	bool binary(JsonValue::binary_t&);
	bool start_object(size_t);
	bool key(JsonValue::string_t&);
	bool end_object();
	bool start_array(size_t);
	bool end_array();
	bool parse_error(size_t, std::string const&, JsonValue::exception const& error);

	std::string const& GetError() const;

private:

	std::string m_error;
};
} // namespace Klip::Json
