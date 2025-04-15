#pragma once

template<typename TOKEN>
requires std::is_same_v<TOKEN, clang::Token> || std::is_same_v<TOKEN, PPToken>
void TranslationUnit::add_input_token(clang::SourceLocation current_location, TOKEN const& token)
{
  DoutEntering(dc::notice,
    "TranslationUnit::add_input_token(" << print_source_location(current_location) << ", " << print_token(token) << ")");

  auto [current_offset, token_length] = clang_frontend_.measure_token_length(current_location);
  add_input_token(current_offset, token_length, token);
}

template<typename TOKEN>
requires std::is_same_v<TOKEN, clang::Token> || std::is_same_v<TOKEN, PPToken>
void TranslationUnit::add_input_token(unsigned int current_offset, size_t token_length, TOKEN const& token)
{
  DoutEntering(dc::notice,
    "TranslationUnit::add_input_token(" << current_offset << ", " << token_length << ", " << print_token(token) << ")");

  auto token_view = source_file_.span(current_offset, token_length);
  Dout(dc::notice, "New token to add: \"" << buf2str(token_view) << "\".");

  // Process the characters that were skipped (whitespace and comments- plus optional backslash-newlines).
  process_gap(current_offset);

  // Create an InputToken.
  Dout(dc::notice, "Adding token \"" << buf2str(token_view) << "\".");
  InputToken input_token(token, token_view);

  // Update last_offset to the position after the current token.
  last_offset_ = current_offset + token_length;
}
