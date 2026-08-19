#include "vrs.h"
#include "mora.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kSampleRate = VRS_SAMPLE_RATE;
constexpr int kDefaultPower = 79;
constexpr const char* kInventory = "@AIUEOaiueonyPpTtKkxSsHhFBDGgZzMNRYWc#Q*";
constexpr const char* kVrsSymbols = "AIUEOaiueonyPpTtKkxSsHhFBDGgZzMNRYWc#Q*/012";

struct TtsError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct VibratoSpec {
    bool enabled = false;
    double depth_cents = 30.0;
    double rate_hz = 5.5;
    double fade_ms = 120.0;
};

struct SheetNote {
    std::vector<double> pitches_hz;
    std::vector<char> pitch_ops;
    std::string lyric;
    int duration_ms = 0;
    VibratoSpec vibrato;
};

struct Mora {
    std::vector<char> phones;
};

struct Preset {
    int source_num;
    int source_den;
    int power;
};

const std::map<std::string, Preset> kPresets = {
    {"vibri", {349, 500, kDefaultPower}},
    {"mojibri", {85, 100, kDefaultPower}},
    {"mojiko", {80, 100, kDefaultPower}},
    {"osorezan", {150, 100, kDefaultPower}},
};

int singing_lead_ms(char symbol) {
    // Match openutau
    if (std::strchr("PTKBDG", symbol)) return 18;
    if (std::strchr("ptkgZzMNRYW", symbol)) return 25;
    if (std::strchr("SsxHhF", symbol)) return 35;
    if (symbol == 'y') return 20;
    return 28;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(const std::string& input) {
    size_t first = 0;
    while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first]))) {
        ++first;
    }
    size_t last = input.size();
    while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1]))) {
        --last;
    }
    return input.substr(first, last - first);
}

std::vector<std::string> shell_split(const std::string& input) {
    std::vector<std::string> out;
    std::string current;
    char quote = 0;
    bool escaped = false;

    for (char c : input) {
        if (escaped) {
            current.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\' && quote != '\'') {
            escaped = true;
            continue;
        }
        if (quote) {
            if (c == quote) {
                quote = 0;
            } else {
                current.push_back(c);
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = c;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (escaped) {
        current.push_back('\\');
    }
    if (quote) {
        throw TtsError("Unterminated quote in music sheet.");
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

std::vector<Mora> analyze_vrs(const std::string& text) {
    if (text.find('\n') != std::string::npos || text.find('\r') != std::string::npos) {
        throw TtsError("A lyric cannot contain a line break.");
    }
    std::istringstream stream(text);
    std::string token;
    std::vector<Mora> morae;
    while (stream >> token) {
        Mora mora;
        for (char symbol : token) {
            if (!std::strchr(kVrsSymbols, symbol)) {
                throw TtsError(std::string("Unsupported VRS symbol '") + symbol + "' in '" + text + "'.");
            }
            mora.phones.push_back(symbol);
        }
        if (!mora.phones.empty()) {
            morae.push_back(std::move(mora));
        }
    }
    if (morae.empty()) {
        throw TtsError("The VRS lyric is empty.");
    }
    return morae;
}

std::vector<Mora> analyze_lyric(const std::string& text) {
    std::string value = trim(text);
    if (value.size() >= 2 && value.front() == '[' && value.back() == ']') {
        return analyze_vrs(value.substr(1, value.size() - 2));
    }
    try {
        auto analyzed = mojib_text::analyze(value);
        std::vector<Mora> morae;
        morae.reserve(analyzed.size());
        for (auto& source : analyzed) {
            Mora mora;
            mora.phones = std::move(source.phones);
            morae.push_back(std::move(mora));
        }
        return morae;
    } catch (const std::exception& error) {
        throw TtsError(error.what());
    }
}

bool is_standalone_sokuon(const std::string& lyric) {
    return lyric == u8"っ" || lyric == u8"ッ";
}

bool is_standalone_long_mark(const std::string& lyric) {
    return lyric == u8"ー";
}

bool phone_is_vowel(char phone) {
    switch (phone) {
        case 'A': case 'I': case 'U': case 'E': case 'O':
        case 'a': case 'i': case 'u': case 'e': case 'o':
            return true;
        default:
            return false;
    }
}

std::string infer_long_mark_vrs(const std::vector<SheetNote>& notes) {
    for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
        const std::string lyric = trim(it->lyric);
        const std::string lyric_lower = lower(lyric);
        if (lyric.empty() || lyric_lower == "r" || lyric_lower == "rest" || lyric == "-") {
            continue;
        }
        auto morae = analyze_lyric(lyric);
        for (auto mora_it = morae.rbegin(); mora_it != morae.rend(); ++mora_it) {
            for (auto phone_it = mora_it->phones.rbegin(); phone_it != mora_it->phones.rend(); ++phone_it) {
                if (phone_is_vowel(*phone_it)) {
                    return std::string("[") + *phone_it + "]";
                }
            }
        }
    }
    throw TtsError("'ー' needs a previous vowel to extend.");
}

int phid_of(char symbol) {
    const char* p = std::strchr(kInventory, symbol);
    if (p && p != kInventory) {
        return static_cast<int>(p - kInventory);
    }
    if (symbol == '/' || symbol == '0' || symbol == '1' || symbol == '2') {
        return static_cast<unsigned char>(symbol);
    }
    throw TtsError(std::string("Unsupported VRS symbol '") + symbol + "'.");
}


double midi_to_hz(double midi) {
    return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
}

double parse_pitch(const std::string& value) {
    std::smatch match;
    static const std::regex re(R"(^([A-Ga-g])([#b]?)(-?[0-9]+)([+-][0-9]+(?:\.[0-9]+)?)?$)");
    if (!std::regex_match(value, match, re)) {
        throw TtsError("Invalid pitch '" + value + "'. Use C4 or F#4+25.");
    }
    static const std::map<char, int> names = {
        {'C', 0}, {'D', 2}, {'E', 4}, {'F', 5}, {'G', 7}, {'A', 9}, {'B', 11},
    };
    char name = static_cast<char>(std::toupper(static_cast<unsigned char>(match[1].str()[0])));
    int semitone = names.at(name);
    const std::string accidental = match[2].str();
    if (accidental == "#") semitone++;
    if (accidental == "b") semitone--;
    int octave = std::stoi(match[3].str());
    double cents = match[4].matched ? std::stod(match[4].str()) : 0.0;
    double midi = (octave + 1) * 12 + semitone + cents / 100.0;
    return midi_to_hz(midi);
}

double parse_units(const std::string& text) {
    if (text.empty()) {
        throw TtsError("Missing note length inside '()'");
    }

    auto parse_number = [](const std::string& value, const std::string& whole) -> double {
        try {
            size_t used = 0;
            double parsed = std::stod(value, &used);
            if (used != value.size() || !std::isfinite(parsed)) {
                throw std::invalid_argument("trailing characters");
            }
            return parsed;
        } catch (const std::exception&) {
            throw TtsError("Invalid note length '" + whole + "'.");
        }
    };

    const auto slash = text.find('/');
    double units = 0.0;
    if (slash == std::string::npos) {
        units = parse_number(text, text);
    } else {
        if (text.find('/', slash + 1) != std::string::npos) {
            throw TtsError("Note length '" + text + "' has too many '/'. Use one fraction such as 3/2.");
        }
        if (slash == 0) {
            throw TtsError("Missing numerator in note length '" + text + "'. Use a fraction such as 3/2.");
        }
        if (slash + 1 == text.size()) {
            throw TtsError("Missing denominator in note length '" + text + "'. Use a fraction such as 3/2.");
        }
        const double num = parse_number(text.substr(0, slash), text);
        const double den = parse_number(text.substr(slash + 1), text);
        if (den == 0.0) {
            throw TtsError("The denominator in note length '" + text + "' cannot be 0.");
        }
        units = num / den;
    }

    if (!std::isfinite(units) || units <= 0.0) {
        throw TtsError("Note length '" + text + "' must be greater than 0.");
    }
    return units;
}

int duration_ms(const std::string& length_text, double unit_ms) {
    double units = parse_units(length_text);
    long duration = std::lround(unit_ms * units);
    if (duration < 1 || duration > 32767) {
        throw TtsError("Every note must be between 1 and 32767 milliseconds.");
    }
    return static_cast<int>(duration);
}

struct PitchSpec {
    std::vector<double> points_hz;
    std::vector<char> ops;
};

PitchSpec pitch_curve(const std::string& text) {
    if (text == "R" || text == "r") {
        return {};
    }
    if (text.empty()) {
        throw TtsError("Missing pitch.");
    }
    if (text.front() == '>' || text.front() == '~') {
        throw TtsError("Pitch curve '" + text + "' is missing its starting pitch.");
    }
    if (text.back() == '>' || text.back() == '~') {
        const char op = text.back();
        throw TtsError("Pitch curve '" + text + "' is missing a pitch after '" + std::string(1, op) + "'.");
    }

    PitchSpec result;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '>' || text[i] == '~') {
            if (i == start) {
                throw TtsError("Pitch curve '" + text + "' is missing a pitch between bend operators.");
            }
            result.points_hz.push_back(parse_pitch(text.substr(start, i - start)));
            if (i < text.size()) {
                result.ops.push_back(text[i]);
            }
            start = i + 1;
        }
    }
    return result;
}

double base_pitch_at(const SheetNote& note, double absolute_ms) {
    const auto& curve = note.pitches_hz;
    if (curve.empty()) return 0.0;
    if (curve.size() == 1 || note.duration_ms <= 0) return curve.front();
    if (absolute_ms <= 0.0) return curve.front();
    if (absolute_ms >= note.duration_ms) return curve.back();

    double position = absolute_ms / static_cast<double>(note.duration_ms) * (curve.size() - 1);
    size_t left = std::min(static_cast<size_t>(std::floor(position)), curve.size() - 2);
    double frac = position - static_cast<double>(left);
    if (left < note.pitch_ops.size() && note.pitch_ops[left] == '~') {
        constexpr double kPi = 3.14159265358979323846;
        frac = 0.5 * (1.0 - std::cos(kPi * frac));
    }
    return curve[left] + (curve[left + 1] - curve[left]) * frac;
}

double pitch_at(const SheetNote& note, double absolute_ms) {
    double hz = base_pitch_at(note, absolute_ms);
    if (!note.vibrato.enabled || hz <= 0.0 || note.vibrato.depth_cents == 0.0) {
        return hz;
    }

    constexpr double kPi = 3.14159265358979323846;
    double t_ms = std::clamp(absolute_ms, 0.0, static_cast<double>(note.duration_ms));
    double fade = 1.0;
    if (note.vibrato.fade_ms > 0.0 && t_ms < note.vibrato.fade_ms) {
        double x = std::clamp(t_ms / note.vibrato.fade_ms, 0.0, 1.0);
        fade = 0.5 * (1.0 - std::cos(kPi * x));
    }

    double phase = 2.0 * kPi * note.vibrato.rate_hz * (t_ms / 1000.0);
    double cents = note.vibrato.depth_cents * fade * std::sin(phase);
    return hz * std::pow(2.0, cents / 1200.0);
}

VibratoSpec parse_vibrato(const std::string& modifier) {
    VibratoSpec result;
    result.enabled = true;
    if (modifier == "vib") {
        return result;
    }
    if (modifier.rfind("vib", 0) == 0 && modifier.rfind("vib=", 0) != 0) {
        throw TtsError("Expected '=' after 'vib'.");
    }
    if (modifier.rfind("vib=", 0) != 0) {
        throw TtsError("Unknown sheet modifier '{" + modifier + "}'.");
    }

    std::vector<std::string> args;
    std::string body = modifier.substr(4);
    size_t start = 0;
    while (start <= body.size()) {
        size_t comma = body.find(',', start);
        args.push_back(trim(body.substr(start, comma == std::string::npos ? std::string::npos : comma - start)));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    if (body.empty()) {
        throw TtsError("Missing vibrato values after 'vib='.");
    }
    if (args.size() == 1) {
        if (args[0].empty()) {
            throw TtsError("Missing vibrato values after 'vib='.");
        }
        throw TtsError("Vibrato rate is missing after depth '" + args[0] + "'.");
    }
    if (args.size() > 3) {
        throw TtsError("Too many vibrato values.");
    }
    if (args[0].empty()) {
        const std::string rate = args.size() > 1 && !args[1].empty() ? args[1] : "5.5";
        throw TtsError("Vibrato depth is missing before the first comma.");
    }
    if (args[1].empty()) {
        throw TtsError("Vibrato rate is missing after depth '" + args[0] + "'.");
    }
    if (args.size() == 3 && args[2].empty()) {
        throw TtsError("Vibrato fade is missing after the second comma.");
    }

    auto parse_number = [](const std::string& text, const char* name) -> double {
        try {
            size_t used = 0;
            double value = std::stod(text, &used);
            if (used != text.size() || !std::isfinite(value)) throw std::invalid_argument("trailing characters");
            return value;
        } catch (const std::exception&) {
            throw TtsError(std::string("Vibrato ") + name + " '" + text + "' is not a valid number.");
        }
    };
    result.depth_cents = parse_number(args[0], "depth");
    result.rate_hz = parse_number(args[1], "rate");
    if (args.size() == 3) result.fade_ms = parse_number(args[2], "fade");

    if (result.depth_cents < 0.0 || result.depth_cents > 1200.0) {
        throw TtsError("Vibrato depth must be between 0 and 1200 cents.");
    }
    if (result.rate_hz <= 0.0 || result.rate_hz > 50.0) {
        throw TtsError("Vibrato rate must be greater than 0 and at most 50 Hz.");
    }
    if (result.fade_ms < 0.0 || result.fade_ms > 32767.0) {
        throw TtsError("Vibrato fade must be between 0 and 32767 ms.");
    }
    return result;
}

std::vector<SheetNote> parse_sheet_notes(std::string sheet, double unit_ms) {
    for (char& c : sheet) {
        if (c == ';' || c == '|') {
            c = ' ';
        }
    }

    const auto tokens = shell_split(sheet);
    std::vector<SheetNote> notes;
    PitchSpec current_pitch;
    bool have_pitch = false;

    // help
    static const std::regex missing_pitch_colon_re(
        R"(^[A-Ga-g][#b]?-?[0-9]+(?:[+-][0-9]+(?:\.[0-9]+)?)?(?:[>~][A-Ga-g][#b]?-?[0-9]+(?:[+-][0-9]+(?:\.[0-9]+)?)?)*$)");
    static const std::regex incomplete_pitch_curve_re(
        R"(^[A-Ga-g][#b]?-?[0-9]+(?:[+-][0-9]+(?:\.[0-9]+)?)?(?:[>~][A-Ga-g][#b]?-?[0-9]+(?:[+-][0-9]+(?:\.[0-9]+)?)?)*[>~]$)");

    for (const std::string& token : tokens) {
        if (token.empty()) {
            continue;
        }

        const size_t colon_count = static_cast<size_t>(std::count(token.begin(), token.end(), ':'));
        if (colon_count > 1) {
            throw TtsError("Too many ':' characters in '" + token + "'. Use one colon after the pitch.");
        }

        if (std::regex_match(token, missing_pitch_colon_re)) {
            throw TtsError("'" + token + "' looks like a pitch. Did you mean '" + token + ":'?");
        }
        if (std::regex_match(token, incomplete_pitch_curve_re)) {
            const char op = token.back();
            throw TtsError("Pitch curve '" + token + "' is incomplete after '" + std::string(1, op) + "'.");
        }

        if (token.back() == ':') {
            const std::string pitch_text = token.substr(0, token.size() - 1);
            if (pitch_text.empty()) {
                throw TtsError("Missing pitch before ':'.");
            }
            current_pitch = pitch_curve(pitch_text);
            if (current_pitch.points_hz.empty()) {
                throw TtsError("A rest cannot be used as the current pitch.");
            }
            have_pitch = true;
            continue;
        }

        PitchSpec note_pitch = current_pitch;
        bool note_has_pitch = have_pitch;
        std::string value = token;

        const size_t colon = token.find(':');
        if (colon != std::string::npos) {
            if (colon == 0) {
                throw TtsError("Missing pitch before ':' in '" + token + "'.");
            }
            if (colon + 1 >= token.size()) {
                throw TtsError("Missing lyric after ':' in '" + token + "'.");
            }
            note_pitch = pitch_curve(token.substr(0, colon));
            if (note_pitch.points_hz.empty()) {
                throw TtsError("A rest cannot be used as a note pitch.");
            }
            current_pitch = note_pitch;
            have_pitch = note_has_pitch = true;
            value = token.substr(colon + 1);
        }

        VibratoSpec vibrato;
        const size_t open_count = static_cast<size_t>(std::count(value.begin(), value.end(), '{'));
        const size_t close_count = static_cast<size_t>(std::count(value.begin(), value.end(), '}'));
        if (open_count != close_count) {
            if (open_count > close_count) {
                throw TtsError("Unclosed sheet modifier in '" + token + "'. Missing '}'.");
            }
            throw TtsError("Unexpected '}' in '" + token + "'. Sheet modifiers must start with '{'.");
        }
        if (open_count > 1) {
            throw TtsError("Only one sheet modifier is allowed per note in '" + token + "'.");
        }
        if (open_count == 1) {
            const size_t open = value.find('{');
            const size_t close = value.find('}', open + 1);
            if (open == 0) {
                throw TtsError("Missing lyric before the sheet modifier in '" + token + "'.");
            }
            if (close + 1 != value.size()) {
                const std::string modifier_text = value.substr(open, close - open + 1);
                const std::string before = value.substr(0, open);
                const std::string after = value.substr(close + 1);
                if (!after.empty() && after.front() == '(' && after.back() == ')') {
                    throw TtsError("Vibrato must come after the note length.");
                }
                throw TtsError("Sheet vibrato '" + modifier_text + "' must be the last part of the note in '" + token + "'.");
            }
            const std::string modifier_body = value.substr(open + 1, close - open - 1);
            if (modifier_body.empty()) {
                throw TtsError("Empty sheet '{}'. Did you mean '{vib}'?");
            }
            vibrato = parse_vibrato(modifier_body);
            value = value.substr(0, open);
        } else if (value.find("vib=") != std::string::npos) {
            throw TtsError("Vibrato must be inside braces.");
        }

        std::string lyric = value;
        std::string length_text = "1";
        const size_t open_parens = static_cast<size_t>(std::count(value.begin(), value.end(), '('));
        const size_t close_parens = static_cast<size_t>(std::count(value.begin(), value.end(), ')'));
        if (open_parens != 0 || close_parens != 0) {
            if (open_parens == 0) {
                throw TtsError("Unexpected ')' in '" + token + "'.");
            }
            if (close_parens == 0) {
                throw TtsError("Missing ')' after the note length in '" + token + "'.");
            }
            if (open_parens > 1 || close_parens > 1) {
                throw TtsError("Only one note length is allowed in '" + token + "'.");
            }
            const size_t open = value.find('(');
            const size_t close = value.find(')');
            if (close < open) {
                throw TtsError("')' appears before '(' in '" + token + "'.");
            }
            if (open == 0) {
                throw TtsError("Missing lyric before the note length in '" + token + "'.");
            }
            if (close + 1 != value.size()) {
                const std::string trailing = value.substr(close + 1);
                if (trailing.rfind("vib", 0) == 0) {
                    throw TtsError("Vibrato must be inside braces after the timing.");
                }
                throw TtsError("Unexpected text '" + trailing + "' after the note length in '" + token + "'.");
            }
            lyric = value.substr(0, open);
            length_text = value.substr(open + 1, close - open - 1);
            if (length_text.empty()) {
                throw TtsError("Missing note length inside '()' in '" + token + "'. ");
            }
        }

        if (lyric.empty()) {
            throw TtsError("Missing lyric in '" + token + "'.");
        }

        const std::string lyric_lower = lower(lyric);
        if (lyric_lower == "r" || lyric_lower == "rest" || lyric == "-") {
            if (vibrato.enabled) {
                throw TtsError("Vibrato cannot be applied to a rest.");
            }
            SheetNote note;
            note.duration_ms = duration_ms(length_text, unit_ms);
            notes.push_back(std::move(note));
            continue;
        }

        if (!note_has_pitch) {
            throw TtsError("'" + token + "' has no pitch.");
        }

        if (is_standalone_sokuon(lyric)) {
            lyric = "[Q]";
        } else if (is_standalone_long_mark(lyric)) {
            lyric = infer_long_mark_vrs(notes);
        }

        SheetNote note;
        note.pitches_hz = std::move(note_pitch.points_hz);
        note.pitch_ops = std::move(note_pitch.ops);
        note.lyric = lyric;
        note.duration_ms = duration_ms(length_text, unit_ms);
        note.vibrato = vibrato;
        notes.push_back(std::move(note));
    }

    return notes;
}

std::vector<SheetNote> parse_sheet(const std::string& sheet, int bpm, int time_den) {
    if (bpm < 20 || bpm > 400) {
        throw TtsError("BPM must be between 20 and 400.");
    }
    if (!(time_den == 1 || time_den == 2 || time_den == 4 || time_den == 8 ||
          time_den == 16 || time_den == 32 || time_den == 64)) {
        throw TtsError("time_den must be 1, 2, 4, 8, 16, 32, or 64.");
    }

    double unit_ms = 240000.0 / (static_cast<double>(bpm) * time_den);
    auto notes = parse_sheet_notes(sheet, unit_ms);
    if (notes.empty()) {
        throw TtsError("The music sheet is empty.");
    }
    long long total = 0;
    for (const auto& note : notes) total += note.duration_ms;
    if (total > 240000) {
        throw TtsError("A music sheet can be at most four minutes long.");
    }
    return notes;
}

std::vector<SheetNote> make_quick_notes(int bpm, const std::string& pitch, const std::string& phrase) {
    if (bpm < 20 || bpm > 400) {
        throw TtsError("BPM must be between 20 and 400.");
    }
    double hz = parse_pitch(pitch);
    auto morae = analyze_lyric(phrase);
    int ms = static_cast<int>(std::lround(30000.0 / bpm));
    std::vector<SheetNote> notes;
    for (const auto& mora : morae) {
        std::string lyric(mora.phones.begin(), mora.phones.end());
        SheetNote note;
        note.pitches_hz = {hz};
        note.lyric = "[" + lyric + "]";
        note.duration_ms = ms;
        notes.push_back(std::move(note));
    }
    return notes;
}

class Model {
public:
    explicit Model(const fs::path& assets) {
        std::memset(&model_, 0, sizeof(model_));
        if (!vrs_model_load(&model_, assets.string().c_str())) {
            throw TtsError("Could not load Mojib Ribbon voice data from '" + assets.string() + "'.");
        }
    }
    ~Model() { vrs_model_free(&model_); }
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;
    VrsModel* get() { return &model_; }
private:
    VrsModel model_{};
};

void append_silence(std::vector<int16_t>& pcm, int duration_ms_value) {
    size_t count = static_cast<size_t>(std::lround(duration_ms_value * static_cast<double>(kSampleRate) / 1000.0));
    pcm.insert(pcm.end(), count, 0);
}

std::vector<int> make_singing_durations(const std::vector<char>& symbols,
                                        const std::vector<int>& mora_ends,
                                        const std::vector<int>& owners,
                                        const std::vector<SheetNote>& notes) {
    std::vector<int> durations(symbols.size(), 0);
    size_t event = 0;

    for (size_t note_index = 0; note_index < notes.size(); ++note_index) {
        size_t note_begin = event;
        while (event < owners.size() && owners[event] == static_cast<int>(note_index)) {
            ++event;
        }
        const size_t note_end = event;
        if (note_begin == note_end) continue;

        int mora_count = 0;
        for (size_t i = note_begin; i < note_end; ++i) {
            if (mora_ends[i]) ++mora_count;
        }
        if (mora_count <= 0 || !mora_ends[note_end - 1]) {
            throw TtsError("Invalid mora grouping in singing note.");
        }

        size_t mora_begin = note_begin;
        int mora_index = 0;
        while (mora_begin < note_end) {
            size_t mora_end = mora_begin;
            while (mora_end < note_end && !mora_ends[mora_end]) ++mora_end;
            if (mora_end >= note_end) {
                throw TtsError("Invalid unterminated mora in singing note.");
            }

            const int target_begin = notes[note_index].duration_ms * mora_index / mora_count;
            const int target_end = notes[note_index].duration_ms * (mora_index + 1) / mora_count;
            const int target = target_end - target_begin;
            const int phone_count = static_cast<int>(mora_end - mora_begin + 1);
            if (target < phone_count) {
                throw TtsError("A singing note is too short for its phoneme count.");
            }

            if (phone_count == 1) {
                durations[mora_begin] = target;
            } else {
                std::vector<int> desired;
                desired.reserve(static_cast<size_t>(phone_count - 1));
                int desired_total = 0;
                for (size_t i = mora_begin; i < mora_end; ++i) {
                    int lead = std::max(1, singing_lead_ms(symbols[i]));
                    desired.push_back(lead);
                    desired_total += lead;
                }

                const int lead_count = phone_count - 1;
                int lead_budget = std::min(desired_total, target - 1);
                lead_budget = std::max(lead_count, lead_budget);

                if (lead_budget == desired_total) {
                    for (int j = 0; j < lead_count; ++j) {
                        durations[mora_begin + static_cast<size_t>(j)] = desired[j];
                    }
                } else {
                    const int extra_budget = lead_budget - lead_count;
                    int extra_weight_total = 0;
                    for (int v : desired) extra_weight_total += std::max(0, v - 1);
                    int previous = 0;
                    for (int j = 0; j < lead_count; ++j) {
                        int cumulative_weight = 0;
                        for (int k = 0; k <= j; ++k) cumulative_weight += std::max(0, desired[k] - 1);
                        int cumulative = extra_weight_total > 0
                            ? (extra_budget * cumulative_weight) / extra_weight_total
                            : 0;
                        durations[mora_begin + static_cast<size_t>(j)] = 1 + cumulative - previous;
                        previous = cumulative;
                    }
                }

                int used = 0;
                for (size_t i = mora_begin; i < mora_end; ++i) used += durations[i];
                durations[mora_end] = target - used;
            }

            mora_begin = mora_end + 1;
            ++mora_index;
        }
    }

    return durations;
}

std::vector<int16_t> render_phrase(VrsModel* model, const Preset& preset,
                                   const std::vector<SheetNote>& notes) {
    std::vector<char> symbols;
    std::vector<int> mora_ends;
    std::vector<int> owners;

    std::vector<std::vector<Mora>> morae_by_note;
    morae_by_note.reserve(notes.size());
    for (const auto& note : notes) {
        morae_by_note.push_back(analyze_lyric(note.lyric));
    }

    for (size_t note_index = 0; note_index < notes.size(); ++note_index) {
        const auto& morae = morae_by_note[note_index];
        for (const auto& mora : morae) {
            const auto& phones = mora.phones;
            for (size_t phone_index = 0; phone_index < phones.size(); ++phone_index) {
                symbols.push_back(phones[phone_index]);
                mora_ends.push_back(phone_index + 1 == phones.size() ? 1 : 0);
                owners.push_back(static_cast<int>(note_index));
            }
        }
    }

    if (symbols.empty()) return {};

    VrsLine line{};
    line.count = static_cast<int>(symbols.size());
    line.events = static_cast<VrsEvent*>(std::calloc(symbols.size(), sizeof(VrsEvent)));
    if (!line.events) throw std::bad_alloc();
    auto cleanup_line = [&]() { vrs_line_free(&line); };

    try {
        for (int i = 0; i < line.count; ++i) {
            line.events[i].phid = phid_of(symbols[i]);
            line.events[i].symbol = symbols[i];
            line.events[i].phrase_end = 0;
        }

        const std::vector<int> durations = make_singing_durations(symbols, mora_ends, owners, notes);
        std::vector<int> owner_elapsed(notes.size(), 0);

        for (int i = 0; i < line.count; ++i) {
            VrsEvent& event = line.events[i];
            const int owner = owners[i];
            const SheetNote& note = notes[owner];
            const int duration = durations[i];
            if (duration <= 0 || duration > 32767) {
                throw TtsError("Singing timing produced an invalid phoneme duration.");
            }

            event.duration_ms = duration;
            event.duration_was_explicit = 1;

            event.power_count = 2;
            event.power_values_raw = static_cast<int16_t*>(std::malloc(2 * sizeof(int16_t)));
            event.power_positions_ms = static_cast<int16_t*>(std::malloc(2 * sizeof(int16_t)));
            if (!event.power_values_raw || !event.power_positions_ms) throw std::bad_alloc();
            event.power_values_raw[0] = static_cast<int16_t>(preset.power);
            event.power_values_raw[1] = static_cast<int16_t>(preset.power);
            event.power_positions_ms[0] = 0;
            event.power_positions_ms[1] = static_cast<int16_t>(duration);
            event.power_raw = event.power_values_raw[0];

            const auto& curve = note.pitches_hz;
            const int event_start = owner_elapsed[owner];
            const int event_end = event_start + duration;
            std::vector<int> points = {0, duration};

            const bool has_cosine =
                std::find(note.pitch_ops.begin(), note.pitch_ops.end(), '~') != note.pitch_ops.end();
            if (has_cosine || note.vibrato.enabled) {
                for (int local_ms = 1; local_ms < duration; ++local_ms) {
                    points.push_back(local_ms);
                }
            } else if (curve.size() > 1) {
                for (size_t point = 1; point + 1 < curve.size(); ++point) {
                    int absolute = static_cast<int>(std::lround(
                        note.duration_ms * static_cast<double>(point) /
                        static_cast<double>(curve.size() - 1)));
                    if (event_start < absolute && absolute < event_end) {
                        points.push_back(absolute - event_start);
                    }
                }
            }
            std::sort(points.begin(), points.end());
            points.erase(std::unique(points.begin(), points.end()), points.end());

            event.pitch_count = static_cast<int>(points.size());
            event.pitch_values_millihz = static_cast<int32_t*>(
                std::malloc(points.size() * sizeof(int32_t)));
            event.pitch_positions_ms = static_cast<int16_t*>(
                std::malloc(points.size() * sizeof(int16_t)));
            if (!event.pitch_values_millihz || !event.pitch_positions_ms) throw std::bad_alloc();

            for (size_t p = 0; p < points.size(); ++p) {
                const int local_ms = points[p];
                const int absolute_ms = std::min(note.duration_ms, event_start + local_ms);
                const double hz = pitch_at(note, absolute_ms);
                event.pitch_values_millihz[p] = static_cast<int32_t>(std::lround(hz * 1000.0));
                event.pitch_positions_ms[p] = static_cast<int16_t>(local_ms);
            }
            event.pitch_millihz = event.pitch_values_millihz[0];
            owner_elapsed[owner] += duration;
            event.phrase_end = 0;
        }
        line.events[line.count - 1].phrase_end = 1;

        int16_t* raw_pcm = nullptr;
        int raw_count = 0;
        if (!vrs_render_line(model, &line, preset.source_num, preset.source_den,
                             &raw_pcm, &raw_count, 0) || !raw_pcm || raw_count < 0) {
            std::free(raw_pcm);
            throw TtsError("TTS synthesis failed.");
        }
        std::vector<int16_t> result(raw_pcm, raw_pcm + raw_count);
        std::free(raw_pcm);
        cleanup_line();
        return result;
    } catch (...) {
        cleanup_line();
        throw;
    }
}

void render_to_wav(const fs::path& assets, const std::string& preset_name,
                   const std::vector<SheetNote>& notes, const fs::path& output) {
    auto preset_it = kPresets.find(lower(preset_name));
    if (preset_it == kPresets.end()) {
        throw TtsError("Unknown preset '" + preset_name + "'. Use: vibri, mojibri, mojiko, osorezan.");
    }

    Model model(assets);
    std::vector<int16_t> pcm;
    std::vector<SheetNote> phrase;
    for (const auto& note : notes) {
        if (!note.pitches_hz.empty()) {
            phrase.push_back(note);
        } else {
            if (!phrase.empty()) {
                auto rendered = render_phrase(model.get(), preset_it->second, phrase);
                pcm.insert(pcm.end(), rendered.begin(), rendered.end());
                phrase.clear();
            }
            append_silence(pcm, note.duration_ms);
        }
    }
    if (!phrase.empty()) {
        auto rendered = render_phrase(model.get(), preset_it->second, phrase);
        pcm.insert(pcm.end(), rendered.begin(), rendered.end());
    }

    // CSpdSynth finishes every 22050hz VRS render with this
    // 2x linear interpolation
    int16_t* finished_pcm = nullptr;
    int finished_count = 0;
    if (!vrs_upsample2x(pcm.data(), static_cast<int>(pcm.size()), &finished_pcm, &finished_count) ||
        !finished_pcm || finished_count < 0) {
        std::free(finished_pcm);
        throw TtsError("Could not apply the retail 44.1 kHz VRS finish.");
    }

    fs::create_directories(output.parent_path().empty() ? fs::path(".") : output.parent_path());
    const bool wrote = vrs_write_wav(output.string().c_str(), finished_pcm, finished_count, 44100);
    std::free(finished_pcm);
    if (!wrote) {
        throw TtsError("Could not write output WAV '" + output.string() + "'.");
    }
}

struct Args {
    std::string command;
    fs::path assets;
    fs::path output;
    std::string preset;
    std::string sheet;
    std::string text;
    fs::path sheet_file;
    fs::path text_file;
    std::string note;
    int bpm = 0;
    int time_den = 0;
};

std::string require_value(int& i, int argc, char** argv, const std::string& option) {
    if (i + 1 >= argc) throw TtsError("Missing value for " + option + ".");
    return argv[++i];
}

Args parse_args(int argc, char** argv) {
    if (argc < 2) {
        throw TtsError("Usage: mojib_tts <sheet|quick> --assets DIR --preset NAME --bpm N --out FILE [...]");
    }
    Args args;
    args.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string option = argv[i];
        if (option == "--assets") args.assets = require_value(i, argc, argv, option);
        else if (option == "--out") args.output = require_value(i, argc, argv, option);
        else if (option == "--preset") args.preset = require_value(i, argc, argv, option);
        else if (option == "--bpm") args.bpm = std::stoi(require_value(i, argc, argv, option));
        else if (option == "--time-den") args.time_den = std::stoi(require_value(i, argc, argv, option));
        else if (option == "--sheet") args.sheet = require_value(i, argc, argv, option);
        else if (option == "--sheet-file") args.sheet_file = require_value(i, argc, argv, option);
        else if (option == "--text") args.text = require_value(i, argc, argv, option);
        else if (option == "--text-file") args.text_file = require_value(i, argc, argv, option);
        else if (option == "--note") args.note = require_value(i, argc, argv, option);
        else throw TtsError("Unknown option '" + option + "'.");
    }

    if (args.assets.empty()) throw TtsError("--assets is required.");
    if (args.output.empty()) throw TtsError("--out is required.");
    if (args.preset.empty()) throw TtsError("--preset is required.");
    if (args.bpm == 0) throw TtsError("--bpm is required.");

    if (args.command == "sheet") {
        if (args.time_den == 0) throw TtsError("--time-den is required for sheet mode.");
        if (args.sheet.empty() && args.sheet_file.empty()) throw TtsError("--sheet or --sheet-file is required for sheet mode.");
    } else if (args.command == "quick") {
        if (args.note.empty()) throw TtsError("--note is required for quick mode.");
        if (args.text.empty() && args.text_file.empty()) throw TtsError("--text or --text-file is required for quick mode.");
    } else {
        throw TtsError("Unknown command '" + args.command + "'. Use sheet or quick.");
    }
    return args;
}

std::string read_utf8_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw TtsError("Could not open UTF-8 input file '" + path.string() + "'.");
    std::ostringstream data;
    data << stream.rdbuf();
    std::string text = data.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Args args = parse_args(argc, argv);
        std::vector<SheetNote> notes;
        if (args.command == "sheet") {
            std::string sheet = args.sheet_file.empty() ? args.sheet : read_utf8_file(args.sheet_file);
            notes = parse_sheet(sheet, args.bpm, args.time_den);
        } else {
            std::string text = args.text_file.empty() ? args.text : read_utf8_file(args.text_file);
            notes = make_quick_notes(args.bpm, args.note, text);
        }
        render_to_wav(args.assets, args.preset, notes, args.output);
        return 0;
    } catch (const TtsError& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 3;
    }
}
