import re


PITCH = r"[A-Ga-g][#b]?-?[0-9]+(?:[+-][0-9]+(?:\.[0-9]+)?)?"
BARE_PITCH = re.compile(rf"^{PITCH}(?:[>~]{PITCH})*$")

ROMAJI_ = re.compile(r"[A-Za-z']+")
NOTE_LENGTH = re.compile(r"^(.+?)\(([^()]*)\)$")


ROMAJI = {
    # Vowels
    "a": "あ", "i": "い", "u": "う", "e": "え", "o": "お",

    # Basic kana
    "ka": "か", "ki": "き", "ku": "く", "ke": "け", "ko": "こ",
    "sa": "さ", "shi": "し", "si": "し", "su": "す", "se": "せ", "so": "そ",
    "ta": "た", "chi": "ち", "ti": "ち", "tsu": "つ", "tu": "つ", "te": "て", "to": "と",
    "na": "な", "ni": "に", "nu": "ぬ", "ne": "ね", "no": "の",
    "ha": "は", "hi": "ひ", "fu": "ふ", "hu": "ふ", "he": "へ", "ho": "ほ",
    "ma": "ま", "mi": "み", "mu": "む", "me": "め", "mo": "も",
    "ya": "や", "yu": "ゆ", "yo": "よ",
    "ra": "ら", "ri": "り", "ru": "る", "re": "れ", "ro": "ろ",
    "wa": "わ", "wo": "を", "wi": "うぃ", "we": "うぇ",

    # Voiced / semi-voiced
    "ga": "が", "gi": "ぎ", "gu": "ぐ", "ge": "げ", "go": "ご",
    "za": "ざ", "ji": "じ", "zi": "じ", "zu": "ず", "ze": "ぜ", "zo": "ぞ",
    "da": "だ", "di": "ぢ", "du": "づ", "de": "で", "do": "ど",
    "ba": "ば", "bi": "び", "bu": "ぶ", "be": "べ", "bo": "ぼ",
    "pa": "ぱ", "pi": "ぴ", "pu": "ぷ", "pe": "ぺ", "po": "ぽ",

    # Small-y combinations
    "kya": "きゃ", "kyu": "きゅ", "kyo": "きょ",
    "gya": "ぎゃ", "gyu": "ぎゅ", "gyo": "ぎょ",
    "sha": "しゃ", "shu": "しゅ", "sho": "しょ",
    "sya": "しゃ", "syu": "しゅ", "syo": "しょ",
    "ja": "じゃ", "ju": "じゅ", "jo": "じょ",
    "jya": "じゃ", "jyu": "じゅ", "jyo": "じょ",
    "zya": "じゃ", "zyu": "じゅ", "zyo": "じょ",
    "cha": "ちゃ", "chu": "ちゅ", "cho": "ちょ",
    "cya": "ちゃ", "cyu": "ちゅ", "cyo": "ちょ",
    "tya": "ちゃ", "tyu": "ちゅ", "tyo": "ちょ",
    "dya": "ぢゃ", "dyu": "ぢゅ", "dyo": "ぢょ",
    "nya": "にゃ", "nyu": "にゅ", "nyo": "にょ",
    "hya": "ひゃ", "hyu": "ひゅ", "hyo": "ひょ",
    "bya": "びゃ", "byu": "びゅ", "byo": "びょ",
    "pya": "ぴゃ", "pyu": "ぴゅ", "pyo": "ぴょ",
    "mya": "みゃ", "myu": "みゅ", "myo": "みょ",
    "rya": "りゃ", "ryu": "りゅ", "ryo": "りょ",

    # Foreign sounds
    "fa": "ふぁ", "fi": "ふぃ", "fe": "ふぇ", "fo": "ふぉ",
    "fya": "ふゃ", "fyu": "ふゅ", "fyo": "ふょ",
    "va": "ゔぁ", "vi": "ゔぃ", "vu": "ゔ", "ve": "ゔぇ", "vo": "ゔぉ",
    "vya": "ゔゃ", "vyu": "ゔゅ", "vyo": "ゔょ",
    "she": "しぇ", "je": "じぇ", "che": "ちぇ",
    "tsa": "つぁ", "tsi": "つぃ", "tse": "つぇ", "tso": "つぉ",
    "thi": "てぃ", "thu": "てゅ", "the": "てぇ", "tho": "てょ",
    "dhi": "でぃ", "dhu": "でゅ", "dhe": "でぇ", "dho": "でょ",
    "kwa": "くぁ", "kwi": "くぃ", "kwe": "くぇ", "kwo": "くぉ",
    "gwa": "ぐぁ", "gwi": "ぐぃ", "gwe": "ぐぇ", "gwo": "ぐぉ",

    # Explicit small kana
    "xa": "ぁ", "xi": "ぃ", "xu": "ぅ", "xe": "ぇ", "xo": "ぉ",
    "la": "ぁ", "li": "ぃ", "lu": "ぅ", "le": "ぇ", "lo": "ぉ",
    "xya": "ゃ", "xyu": "ゅ", "xyo": "ょ",
    "lya": "ゃ", "lyu": "ゅ", "lyo": "ょ",
    "xtsu": "っ", "xtu": "っ",
    "ltsu": "っ", "ltu": "っ",
    "xwa": "ゎ", "lwa": "ゎ",
}


def convert_romaji_word(word: str) -> str | None:

    word = word.lower()
    result = []
    i = 0

    while i < len(word):
        char = word[i]

        # n -> ん
        if char == "n":
            if i + 1 == len(word):
                result.append("ん")
                i += 1
                continue

            next_char = word[i + 1]

            if next_char == "'":
                result.append("ん")
                i += 2
                continue

            if next_char == "n":
                result.append("ん")
                i += 2 if i + 2 == len(word) else 1
                continue

            if next_char not in "aiueoy":
                result.append("ん")
                i += 1
                continue

        # gakkou -> がっこう
        if (
            i + 1 < len(word)
            and char == word[i + 1]
            and char.isalpha()
            and char not in "aeioun"
        ):
            result.append("っ")
            i += 1
            continue

        # Try the longest romaji spelling first.
        for length in (4, 3, 2, 1):
            part = word[i:i + length]

            if part in ROMAJI:
                result.append(ROMAJI[part])
                i += length
                break
        else:
            return None

    return "".join(result)


def romaji_to_hiragana(text: str) -> str:

    def replace(match: re.Match[str]) -> str:
        word = match.group(0)
        converted = convert_romaji_word(word)
        return converted if converted is not None else word

    return ROMAJI_.sub(replace, text)


def split_sheet(sheet: str) -> list[str]:

    sheet = sheet.replace(";", " ").replace("|", " ")

    tokens = []
    current = []
    quote = None
    escaped = False

    for char in sheet:
        if escaped:
            current.append(char)
            escaped = False
            continue

        if char == "\\" and quote != "'":
            escaped = True
            continue

        if quote:
            if char == quote:
                quote = None
            else:
                current.append(char)
            continue

        if char in ("'", '"'):
            quote = char
            continue

        if char.isspace():
            if current:
                tokens.append("".join(current))
                current.clear()
            continue

        current.append(char)

    if escaped:
        current.append("\\")

    if quote:
        raise ValueError("Unterminated quote in music sheet.")

    if current:
        tokens.append("".join(current))

    return tokens


def convert_sheet_token(token: str) -> str:
    if not token:
        return token

    # Accept C5 as shorthand for C5:.
    if BARE_PITCH.fullmatch(token):
        return token + ":"

    if token.endswith(":"):
        return token

    prefix = ""
    value = token

    # Separate C5:lyric into its pitch and lyric parts.
    if ":" in token:
        pitch, value = token.split(":", 1)

        if not pitch or not value:
            return token

        prefix = pitch + ":"

    # Pull off an msheet modifier such as { ... }.
    modifier = ""

    if value.endswith("}"):
        brace = value.rfind("{")

        if brace > 0:
            modifier = value[brace:]
            value = value[:brace]

    # Pull off an explicit note length.
    lyric = value
    length = ""

    match = NOTE_LENGTH.fullmatch(value)
    if match:
        lyric = match.group(1)
        length = f"({match.group(2)})"

    # Rests and raw VRS don't need romaji conversion.
    if lyric.lower() in ("r", "rest") or lyric == "-":
        return prefix + lyric + length + modifier

    if lyric.startswith("[") and lyric.endswith("]"):
        return prefix + lyric + length + modifier

    lyric = romaji_to_hiragana(lyric)

    return prefix + lyric + length + modifier


def quote_sheet_token(token: str) -> str:
    if not any(char.isspace() for char in token):
        return token

    token = token.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{token}"'


def convert_sheet_romaji(sheet: str) -> str:

    tokens = split_sheet(sheet)
    tokens = [convert_sheet_token(token) for token in tokens]
    tokens = [quote_sheet_token(token) for token in tokens]

    return " ".join(tokens)
