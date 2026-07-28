#include <cassert>
#include <cctype>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct PaginatedPage {
  std::string text;
  bool is_newline_end{false};
};

struct Token {
  std::string text;
  bool ends_with_hyphen{false};
};

static std::vector<Token> tokenize_word(const std::string &word) {
  std::vector<Token> tokens;
  if (word.empty())
    return tokens;

  bool all_hyphens = true;
  for (char c : word) {
    if (c != '-') {
      all_hyphens = false;
      break;
    }
  }
  if (all_hyphens) {
    tokens.push_back({word, false});
    return tokens;
  }

  size_t start = 0;
  for (size_t i = 0; i < word.length(); i++) {
    if (word[i] == '-' && i < word.length() - 1) {
      tokens.push_back({word.substr(start, i - start + 1), true});
      start = i + 1;
    }
  }
  if (start < word.length()) {
    tokens.push_back({word.substr(start), false});
  }
  return tokens;
}

static std::vector<PaginatedPage> paginate_string(const std::string &input_string, size_t module_count) {
  std::vector<PaginatedPage> paginated_lines;
  if (input_string.empty() || module_count == 0) {
    return paginated_lines;
  }

  std::vector<std::string> raw_lines;
  size_t start = 0;
  size_t end = input_string.find('\n');
  while (end != std::string::npos) {
    std::string line = input_string.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    raw_lines.push_back(line);
    start = end + 1;
    end = input_string.find('\n', start);
  }
  std::string line = input_string.substr(start);
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  raw_lines.push_back(line);

  for (const auto &raw_line : raw_lines) {
    if (raw_line.length() <= module_count) {
      paginated_lines.push_back({raw_line, true});
      continue;
    }

    std::stringstream ss(raw_line);
    std::string word;
    std::vector<PaginatedPage> line_pages;
    std::string current_page = "";
    bool prev_token_ended_with_hyphen = false;

    while (ss >> word) {
      std::vector<Token> word_tokens = tokenize_word(word);
      for (auto &tok : word_tokens) {
        while (tok.text.length() > module_count) {
          if (!current_page.empty()) {
            line_pages.push_back({current_page, false});
            current_page = "";
            prev_token_ended_with_hyphen = false;
          }
          if (module_count > 1) {
            size_t chunk_len = module_count - 1;
            std::string chunk = tok.text.substr(0, chunk_len) + "-";
            line_pages.push_back({chunk, false});
            tok.text = tok.text.substr(chunk_len);
          } else {
            std::string chunk = tok.text.substr(0, 1);
            line_pages.push_back({chunk, false});
            tok.text = tok.text.substr(1);
          }
        }

        if (tok.text.empty()) {
          continue;
        }

        std::string candidate;
        if (current_page.empty()) {
          candidate = tok.text;
        } else if (prev_token_ended_with_hyphen) {
          candidate = current_page + tok.text;
        } else {
          candidate = current_page + " " + tok.text;
        }

        if (candidate.length() <= module_count) {
          current_page = candidate;
          prev_token_ended_with_hyphen = tok.ends_with_hyphen;
        } else {
          if (!current_page.empty()) {
            line_pages.push_back({current_page, false});
          }
          current_page = tok.text;
          prev_token_ended_with_hyphen = tok.ends_with_hyphen;
        }
      }
    }
    if (!current_page.empty()) {
      line_pages.push_back({current_page, false});
    }

    if (!line_pages.empty()) {
      line_pages.back().is_newline_end = true;
      for (const auto &p : line_pages) {
        paginated_lines.push_back(p);
      }
    }
  }

  return paginated_lines;
}

static std::string format_display_page(const std::string &input_string, size_t module_count, bool centering) {
  std::string upper_input = input_string;
  for (char &c : upper_input) {
    c = std::toupper(c);
  }

  std::string display_string = upper_input.substr(0, module_count);
  if (centering) {
    int total_padding = module_count - display_string.length();
    int padding_left = total_padding / 2;
    int padding_right = total_padding - padding_left;
    display_string = std::string(padding_left, ' ') + display_string + std::string(padding_right, ' ');
  } else {
    while (display_string.length() < module_count) {
      display_string += ' ';
    }
  }
  return display_string;
}

int main() {
  std::cout << "Running split flap smart breaking tests..." << std::endl;

  // Test 1: Tokenize Word
  {
    auto tok1 = tokenize_word("state-of-the-art");
    assert(tok1.size() == 4);
    assert(tok1[0].text == "state-" && tok1[0].ends_with_hyphen);
    assert(tok1[1].text == "of-" && tok1[1].ends_with_hyphen);
    assert(tok1[2].text == "the-" && tok1[2].ends_with_hyphen);
    assert(tok1[3].text == "art" && !tok1[3].ends_with_hyphen);
  }

  // Test 2: Paginate short line <= module_count
  {
    auto pages = paginate_string("HELLO", 10);
    assert(pages.size() == 1);
    assert(pages[0].text == "HELLO");
    assert(pages[0].is_newline_end == true);
  }

  // Test 3: Paginate hyphenated word breaking across pages
  {
    auto pages = paginate_string("state-of-the-art technology", 6);
    assert(pages.size() == 6);
    assert(pages[0].text == "state-" && pages[0].is_newline_end == false);
    assert(pages[1].text == "of-" && pages[1].is_newline_end == false);
    assert(pages[2].text == "the-" && pages[2].is_newline_end == false);
    assert(pages[3].text == "art" && pages[3].is_newline_end == false);
    assert(pages[4].text == "techn-" && pages[4].is_newline_end == false);
    assert(pages[5].text == "ology" && pages[5].is_newline_end == true);
  }

  // Test 4: Paginate long word auto-hyphenation
  {
    auto pages = paginate_string("supercalifragilistic", 6);
    assert(pages.size() == 4);
    assert(pages[0].text == "super-" && pages[0].is_newline_end == false);
    assert(pages[1].text == "calif-" && pages[1].is_newline_end == false);
    assert(pages[2].text == "ragil-" && pages[2].is_newline_end == false);
    assert(pages[3].text == "istic" && pages[3].is_newline_end == true);
  }

  // Test 5: Multi-line string with explicit newlines
  {
    auto pages = paginate_string("HELLO WORLD\nNEW LINE", 6);
    assert(pages.size() == 4);
    assert(pages[0].text == "HELLO" && pages[0].is_newline_end == false);
    assert(pages[1].text == "WORLD" && pages[1].is_newline_end == true);
    assert(pages[2].text == "NEW" && pages[2].is_newline_end == false);
    assert(pages[3].text == "LINE" && pages[3].is_newline_end == true);
  }

  // Test 6: Centering vs Left alignment
  {
    std::string centered = format_display_page("HELLO", 10, true);
    assert(centered == "  HELLO   ");

    std::string left_aligned = format_display_page("HELLO", 10, false);
    assert(left_aligned == "HELLO     ");
  }

  std::cout << "All smart breaking unit tests passed successfully!" << std::endl;
  return 0;
}
