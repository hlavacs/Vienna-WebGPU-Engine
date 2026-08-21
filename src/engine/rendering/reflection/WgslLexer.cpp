#include "engine/rendering/reflection/WgslLexer.h"

#include <cctype>
#include <unordered_set>

namespace engine::rendering::reflection
{

namespace
{
const std::unordered_set<std::string_view> kKeywords{
	"struct", "var", "fn", "const", "let", "return",
	"if", "else", "for", "while", "loop", "switch", "case", "default",
	"break", "continue", "true", "false", "discard",
	"uniform", "storage", "private", "workgroup", "function",
	"read", "write", "read_write",
};
}

WgslLexer::WgslLexer(std::string_view source) : m_source(source) {}

char WgslLexer::peek(size_t ahead) const
{
	return (m_pos + ahead < m_source.size()) ? m_source[m_pos + ahead] : '\0';
}

char WgslLexer::advance()
{
	char c = m_source[m_pos++];
	if (c == '\n') { ++m_line; m_column = 1; }
	else           { ++m_column; }
	return c;
}

void WgslLexer::skipWhitespaceAndComments()
{
	while (!isAtEnd())
	{
		char c = peek();
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
		{
			advance();
			continue;
		}
		if (c == '/' && peek(1) == '/')
		{
			// Line comment - skip to end of line.
			while (!isAtEnd() && peek() != '\n')
				advance();
			continue;
		}
		if (c == '/' && peek(1) == '*')
		{
			advance(); advance();
			while (!isAtEnd() && !(peek() == '*' && peek(1) == '/'))
				advance();
			if (!isAtEnd()) { advance(); advance(); }
			continue;
		}
		// Preprocessor directives (#include, #define, #pragma, ...) - WGSL has
		// no native preprocessor. The engine will eventually expand these
		// before reflection runs; until then we skip them silently so the
		// lexer doesn't trip on a line beginning with '#'.
		if (c == '#')
		{
			while (!isAtEnd() && peek() != '\n')
				advance();
			continue;
		}
		break;
	}
}

Token WgslLexer::readIdentifierOrKeyword()
{
	Token t;
	t.line   = m_line;
	t.column = m_column;
	while (!isAtEnd())
	{
		char c = peek();
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
			t.text.push_back(advance());
		else
			break;
	}
	t.kind = (kKeywords.count(t.text) > 0) ? TokenKind::Keyword : TokenKind::Identifier;
	return t;
}

Token WgslLexer::readNumber()
{
	Token t;
	t.line   = m_line;
	t.column = m_column;
	bool isFloat = false;
	bool seenE   = false;
	while (!isAtEnd())
	{
		char c = peek();
		if (std::isdigit(static_cast<unsigned char>(c)) || c == '_')
		{
			t.text.push_back(advance());
		}
		else if (c == '.' && !isFloat)
		{
			isFloat = true;
			t.text.push_back(advance());
		}
		else if ((c == 'e' || c == 'E') && !seenE)
		{
			isFloat = true;
			seenE   = true;
			t.text.push_back(advance());
			if (!isAtEnd() && (peek() == '+' || peek() == '-'))
				t.text.push_back(advance());
		}
		else if (c == 'f' || c == 'h' || c == 'i' || c == 'u')
		{
			// type suffix
			t.text.push_back(advance());
			if (c == 'f' || c == 'h') isFloat = true;
			break;
		}
		else
		{
			break;
		}
	}
	t.kind = isFloat ? TokenKind::FloatLiteral : TokenKind::IntegerLiteral;
	return t;
}

Token WgslLexer::readAttribute()
{
	Token t;
	t.kind   = TokenKind::Attribute;
	t.line   = m_line;
	t.column = m_column;
	advance(); // consume '@'
	while (!isAtEnd())
	{
		char c = peek();
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
			t.text.push_back(advance());
		else
			break;
	}
	return t;
}

Token WgslLexer::readPunct()
{
	Token t;
	t.kind   = TokenKind::Punct;
	t.line   = m_line;
	t.column = m_column;
	t.text.push_back(advance());
	return t;
}

std::vector<Token> WgslLexer::tokenize()
{
	std::vector<Token> out;
	while (!isAtEnd())
	{
		skipWhitespaceAndComments();
		if (isAtEnd())
			break;

		char c = peek();
		if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
		{
			out.push_back(readIdentifierOrKeyword());
		}
		else if (std::isdigit(static_cast<unsigned char>(c)))
		{
			out.push_back(readNumber());
		}
		else if (c == '@')
		{
			out.push_back(readAttribute());
		}
		else
		{
			out.push_back(readPunct());
		}
	}
	Token eof;
	eof.kind   = TokenKind::EndOfFile;
	eof.line   = m_line;
	eof.column = m_column;
	out.push_back(eof);
	return out;
}

} // namespace engine::rendering::reflection
