#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::rendering::reflection
{

/// Token categories the WGSL parser cares about. WGSL has more — we only model
/// what the reflector consumes; everything else (operators inside function
/// bodies etc.) falls into Punct.
enum class TokenKind
{
	EndOfFile,
	Identifier,
	IntegerLiteral,
	FloatLiteral,
	StringLiteral,             ///< quoted string inside an //@ annotation
	Attribute,                 ///< an @ident token (e.g. @group, @binding, @vertex)
	Annotation,                ///< an //@ engine-side annotation comment, stored raw
	Keyword,                   ///< struct, var, fn, const, let, return, if, for, while...
	Punct,                     ///< (, ), {, }, <, >, ,, ;, :, =, etc.
};

struct Token
{
	TokenKind   kind = TokenKind::EndOfFile;
	std::string text;
	uint32_t    line   = 0;
	uint32_t    column = 0;
};

/// Tokenises WGSL source. Strips whitespace and ordinary comments; preserves
/// `//@...` annotation comments as `Annotation` tokens carrying the raw text
/// (everything after the `//@`, untrimmed) so the parser can decode them.
class WgslLexer
{
public:
	explicit WgslLexer(std::string_view source);

	/// Run lexer to completion; returns all tokens with a final EndOfFile.
	std::vector<Token> tokenize();

private:
	std::string_view m_source;
	size_t           m_pos    = 0;
	uint32_t         m_line   = 1;
	uint32_t         m_column = 1;

	[[nodiscard]] bool isAtEnd() const { return m_pos >= m_source.size(); }
	[[nodiscard]] char peek(size_t ahead = 0) const;
	char advance();
	void skipWhitespaceAndComments(std::vector<Token> &out);

	Token readIdentifierOrKeyword();
	Token readNumber();
	Token readAttribute();              ///< @ident
	Token readPunct();
	Token readStringLiteral();
};

} // namespace engine::rendering::reflection
