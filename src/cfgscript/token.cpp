// bytewright - cfgscript token name table.
#include "cfgscript/token.hpp"

namespace bw {
namespace cfgscript {

const char* token_kind_name(TokenKind kind) noexcept {
  switch (kind) {
    case TokenKind::kEof: return "end of input";
    case TokenKind::kIdent: return "identifier";
    case TokenKind::kInt: return "integer";
    case TokenKind::kFloat: return "float";
    case TokenKind::kString: return "string";
    case TokenKind::kTrue: return "'true'";
    case TokenKind::kFalse: return "'false'";
    case TokenKind::kNull: return "'null'";
    case TokenKind::kInclude: return "'include'";
    case TokenKind::kLBrace: return "'{'";
    case TokenKind::kRBrace: return "'}'";
    case TokenKind::kLBracket: return "'['";
    case TokenKind::kRBracket: return "']'";
    case TokenKind::kLParen: return "'('";
    case TokenKind::kRParen: return "')'";
    case TokenKind::kSemicolon: return "';'";
    case TokenKind::kComma: return "','";
    case TokenKind::kAssign: return "'='";
    case TokenKind::kDot: return "'.'";
    case TokenKind::kQuestion: return "'?'";
    case TokenKind::kColon: return "':'";
    case TokenKind::kPlus: return "'+'";
    case TokenKind::kMinus: return "'-'";
    case TokenKind::kStar: return "'*'";
    case TokenKind::kSlash: return "'/'";
    case TokenKind::kPercent: return "'%'";
    case TokenKind::kShl: return "'<<'";
    case TokenKind::kShr: return "'>>'";
    case TokenKind::kLt: return "'<'";
    case TokenKind::kLe: return "'<='";
    case TokenKind::kGt: return "'>'";
    case TokenKind::kGe: return "'>='";
    case TokenKind::kEq: return "'=='";
    case TokenKind::kNe: return "'!='";
    case TokenKind::kAmp: return "'&'";
    case TokenKind::kCaret: return "'^'";
    case TokenKind::kPipe: return "'|'";
    case TokenKind::kAndAnd: return "'&&'";
    case TokenKind::kOrOr: return "'||'";
    case TokenKind::kBang: return "'!'";
    case TokenKind::kTilde: return "'~'";
  }
  return "token";
}

}  // namespace cfgscript
}  // namespace bw
