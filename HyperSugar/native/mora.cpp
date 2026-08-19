#include "mora.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace mojib_text {
namespace {
using U32 = std::u32string;
struct MoraRule { const char32_t* text; std::uint8_t code; };
struct NumericRule { const char32_t* text; std::vector<std::uint8_t> codes; };

static const MoraRule kMoraRules[] = {
    {U"\u30F6", 0x85},
    {U"\u30F5", 0x82},
    {U"\u30F4\u30A9", 0x40},
    {U"\u30F4\u30A7", 0x3F},
    {U"\u30F4\u30A3", 0x3D},
    {U"\u30F4\u30A1", 0x3C},
    {U"\u30F4", 0x3E},
    {U"\u30F3", 0x04},
    {U"\u30F2", 0x09},
    {U"\u30F1", 0x08},
    {U"\u30F0", 0x06},
    {U"\u30EF", 0x0F},
    {U"\u30EE", 0x0F},
    {U"\u30ED", 0x18},
    {U"\u30EC", 0x17},
    {U"\u30EB", 0x16},
    {U"\u30EA\u30E7", 0x1D},
    {U"\u30EA\u30E5", 0x1B},
    {U"\u30EA\u30E3", 0x19},
    {U"\u30EA", 0x15},
    {U"\u30E9", 0x14},
    {U"\u30E8", 0x0E},
    {U"\u30E7", 0x0E},
    {U"\u30E6", 0x0C},
    {U"\u30E5", 0x0C},
    {U"\u30E4", 0x0A},
    {U"\u30E3", 0x0A},
    {U"\u30E2", 0x22},
    {U"\u30E1", 0x21},
    {U"\u30E0", 0x20},
    {U"\u30DF\u30E7", 0x27},
    {U"\u30DF\u30E5", 0x25},
    {U"\u30DF\u30E3", 0x23},
    {U"\u30DF", 0x1F},
    {U"\u30DE", 0x1E},
    {U"\u30DD", 0x72},
    {U"\u30DC", 0x45},
    {U"\u30DB", 0x9F},
    {U"\u30DA", 0x71},
    {U"\u30D9", 0x44},
    {U"\u30D8", 0x9E},
    {U"\u30D7", 0x70},
    {U"\u30D6", 0x43},
    {U"\u30D5\u30A9", 0xA9},
    {U"\u30D5\u30A7", 0xA8},
    {U"\u30D5\u30A3", 0xA6},
    {U"\u30D5\u30A1", 0xA5},
    {U"\u30D5", 0xA7},
    {U"\u30D4\u30E7", 0x77},
    {U"\u30D4\u30E5", 0x75},
    {U"\u30D4\u30E3", 0x73},
    {U"\u30D4", 0x6F},
    {U"\u30D3\u30E7", 0x4A},
    {U"\u30D3\u30E5", 0x48},
    {U"\u30D3\u30E3", 0x46},
    {U"\u30D3", 0x42},
    {U"\u30D2\u30E7", 0xA4},
    {U"\u30D2\u30E5", 0xA2},
    {U"\u30D2\u30E3", 0xA0},
    {U"\u30D2\u30A7", 0xA3},
    {U"\u30D2", 0xA1},
    {U"\u30D1", 0x6E},
    {U"\u30D0", 0x41},
    {U"\u30CF", 0x9B},
    {U"\u30CE", 0x2C},
    {U"\u30CD", 0x2B},
    {U"\u30CC", 0x2A},
    {U"\u30CB\u30E7", 0x31},
    {U"\u30CB\u30E5", 0x2F},
    {U"\u30CB\u30E3", 0x2D},
    {U"\u30CB", 0x29},
    {U"\u30CA", 0x28},
    {U"\u30C9\u30A5", 0x4D},
    {U"\u30C9", 0x4F},
    {U"\u30C8\u30A5", 0x7A},
    {U"\u30C8", 0x7C},
    {U"\u30C7\u30E5", 0x52},
    {U"\u30C7\u30A3", 0x4C},
    {U"\u30C7", 0x4E},
    {U"\u30C6\u30E5", 0x7F},
    {U"\u30C6\u30A3", 0x79},
    {U"\u30C6", 0x7B},
    {U"\u30C5", 0x66},
    {U"\u30C4\u30A9", 0xAE},
    {U"\u30C4\u30A7", 0xAD},
    {U"\u30C4\u30A3", 0xAB},
    {U"\u30C4\u30A1", 0xAA},
    {U"\u30C4", 0xAC},
    {U"\u30C3", 0x03},
    {U"\u30C2\u30E7", 0x6D},
    {U"\u30C2\u30E5", 0x6B},
    {U"\u30C2\u30E3", 0x69},
    {U"\u30C2\u30A7", 0x6C},
    {U"\u30C2", 0x6A},
    {U"\u30C1\u30E7", 0xB3},
    {U"\u30C1\u30E5", 0xB1},
    {U"\u30C1\u30E3", 0xAF},
    {U"\u30C1\u30A7", 0xB2},
    {U"\u30C1", 0xB0},
    {U"\u30C0", 0x4B},
    {U"\u30BF", 0x78},
    {U"\u30BE", 0x68},
    {U"\u30BD", 0x95},
    {U"\u30BC", 0x67},
    {U"\u30BB", 0x94},
    {U"\u30BA\u30A3", 0x65},
    {U"\u30BA", 0x66},
    {U"\u30B9\u30A3", 0x92},
    {U"\u30B9", 0x93},
    {U"\u30B8\u30E7", 0x6D},
    {U"\u30B8\u30E5", 0x6B},
    {U"\u30B8\u30E3", 0x69},
    {U"\u30B8\u30A7", 0x6C},
    {U"\u30B8", 0x6A},
    {U"\u30B7\u30E7", 0x9A},
    {U"\u30B7\u30E5", 0x98},
    {U"\u30B7\u30E3", 0x96},
    {U"\u30B7\u30A7", 0x99},
    {U"\u30B7", 0x97},
    {U"\u30B6", 0x64},
    {U"\u30B5", 0x91},
    {U"\u30B4", 0x59},
    {U"\u30B3\u00B0", 0x36},
    {U"\u30B3\u309C", 0x36},
    {U"\u30B3", 0x86},
    {U"\u30B2", 0x58},
    {U"\u30B1\u00B0", 0x35},
    {U"\u30B1\u309C", 0x35},
    {U"\u30B1", 0x85},
    {U"\u30B0", 0x57},
    {U"\u30AF\u30A9", 0x90},
    {U"\u30AF\u30A7", 0x8F},
    {U"\u30AF\u30A3", 0x8D},
    {U"\u30AF\u30A1", 0x8C},
    {U"\u30AF\u00B0", 0x34},
    {U"\u30AF\u309C", 0x34},
    {U"\u30AF", 0x84},
    {U"\u30AE\u30E7", 0x5E},
    {U"\u30AE\u30E5", 0x5C},
    {U"\u30AE\u30E3", 0x5A},
    {U"\u30AE", 0x56},
    {U"\u30AD\u30E7", 0x8B},
    {U"\u30AD\u30E5", 0x89},
    {U"\u30AD\u30E3", 0x87},
    {U"\u30AD\u00B0\u30E7", 0x3B},
    {U"\u30AD\u00B0\u30E5", 0x39},
    {U"\u30AD\u00B0\u30E3", 0x37},
    {U"\u30AD\u00B0", 0x33},
    {U"\u30AD\u309C\u30E7", 0x3B},
    {U"\u30AD\u309C\u30E5", 0x39},
    {U"\u30AD\u309C\u30E3", 0x37},
    {U"\u30AD\u309C", 0x33},
    {U"\u30AD", 0x83},
    {U"\u30AC", 0x55},
    {U"\u30AB\u00B0", 0x32},
    {U"\u30AB\u309C", 0x32},
    {U"\u30AB", 0x82},
    {U"\u30AA", 0x09},
    {U"\u30A9", 0x09},
    {U"\u30A8", 0x08},
    {U"\u30A7", 0x08},
    {U"\u30A6\u30A9", 0x13},
    {U"\u30A6\u30A7", 0x12},
    {U"\u30A6\u30A3", 0x10},
    {U"\u30A6", 0x07},
    {U"\u30A5", 0x07},
    {U"\u30A4", 0x06},
    {U"\u30A3", 0x06},
    {U"\u30A2", 0x05},
    {U"\u30A1", 0x05},
    {U"\u2212", 0x02},
    {U"\u30FC", 0x02},
    {U"\u301C", 0x02},
};

static const std::unordered_map<std::uint8_t, std::vector<char>> kCodeToVrs = {
    {0x03, {'Q'}},
    {0x04, {'n'}},
    {0x05, {'A'}},
    {0x06, {'I'}},
    {0x07, {'U'}},
    {0x08, {'E'}},
    {0x09, {'O'}},
    {0x0A, {'Y', 'A'}},
    {0x0C, {'Y', 'U'}},
    {0x0E, {'Y', 'O'}},
    {0x0F, {'W', 'A'}},
    {0x10, {'W', 'I'}},
    {0x12, {'W', 'E'}},
    {0x13, {'W', 'O'}},
    {0x14, {'R', 'A'}},
    {0x15, {'R', 'I'}},
    {0x16, {'R', 'U'}},
    {0x17, {'R', 'E'}},
    {0x18, {'R', 'O'}},
    {0x19, {'R', 'y', 'A'}},
    {0x1B, {'R', 'y', 'U'}},
    {0x1D, {'R', 'y', 'O'}},
    {0x1E, {'M', 'A'}},
    {0x1F, {'M', 'I'}},
    {0x20, {'M', 'U'}},
    {0x21, {'M', 'E'}},
    {0x22, {'M', 'O'}},
    {0x23, {'M', 'y', 'A'}},
    {0x25, {'M', 'y', 'U'}},
    {0x27, {'M', 'y', 'O'}},
    {0x28, {'N', 'A'}},
    {0x29, {'N', 'I'}},
    {0x2A, {'N', 'U'}},
    {0x2B, {'N', 'E'}},
    {0x2C, {'N', 'O'}},
    {0x2D, {'N', 'y', 'A'}},
    {0x2F, {'N', 'y', 'U'}},
    {0x31, {'N', 'y', 'O'}},
    {0x32, {'g', 'A'}},
    {0x33, {'g', 'I'}},
    {0x34, {'g', 'U'}},
    {0x35, {'g', 'E'}},
    {0x36, {'g', 'O'}},
    {0x37, {'g', 'y', 'A'}},
    {0x39, {'g', 'y', 'U'}},
    {0x3B, {'g', 'y', 'O'}},
    {0x3C, {'B', 'A'}},
    {0x3D, {'B', 'I'}},
    {0x3E, {'B', 'U'}},
    {0x3F, {'B', 'E'}},
    {0x40, {'B', 'O'}},
    {0x41, {'B', 'A'}},
    {0x42, {'B', 'I'}},
    {0x43, {'B', 'U'}},
    {0x44, {'B', 'E'}},
    {0x45, {'B', 'O'}},
    {0x46, {'B', 'y', 'A'}},
    {0x48, {'B', 'y', 'U'}},
    {0x4A, {'B', 'y', 'O'}},
    {0x4B, {'D', 'A'}},
    {0x4C, {'D', 'I'}},
    {0x4D, {'D', 'U'}},
    {0x4E, {'D', 'E'}},
    {0x4F, {'D', 'O'}},
    {0x52, {'D', 'y', 'U'}},
    {0x55, {'G', 'A'}},
    {0x56, {'G', 'I'}},
    {0x57, {'G', 'U'}},
    {0x58, {'G', 'E'}},
    {0x59, {'G', 'O'}},
    {0x5A, {'G', 'y', 'A'}},
    {0x5C, {'G', 'y', 'U'}},
    {0x5E, {'G', 'y', 'O'}},
    {0x64, {'Z', 'A'}},
    {0x65, {'z', 'I'}},
    {0x66, {'Z', 'U'}},
    {0x67, {'Z', 'E'}},
    {0x68, {'Z', 'O'}},
    {0x69, {'z', 'y', 'A'}},
    {0x6A, {'z', 'I'}},
    {0x6B, {'z', 'y', 'U'}},
    {0x6C, {'z', 'y', 'E'}},
    {0x6D, {'z', 'y', 'O'}},
    {0x6E, {'P', 'A'}},
    {0x6F, {'p', 'I'}},
    {0x70, {'P', 'U'}},
    {0x71, {'P', 'E'}},
    {0x72, {'P', 'O'}},
    {0x73, {'p', 'y', 'A'}},
    {0x75, {'p', 'y', 'U'}},
    {0x77, {'p', 'y', 'O'}},
    {0x78, {'T', 'A'}},
    {0x79, {'T', 'I'}},
    {0x7A, {'T', 'U'}},
    {0x7B, {'T', 'E'}},
    {0x7C, {'T', 'O'}},
    {0x7F, {'t', 'y', 'U'}},
    {0x82, {'K', 'A'}},
    {0x83, {'k', 'I'}},
    {0x84, {'K', 'U'}},
    {0x85, {'K', 'E'}},
    {0x86, {'K', 'O'}},
    {0x87, {'k', 'y', 'A'}},
    {0x89, {'k', 'y', 'U'}},
    {0x8B, {'k', 'y', 'O'}},
    {0x8C, {'K', 'A'}},
    {0x8D, {'K', 'I'}},
    {0x8F, {'K', 'E'}},
    {0x90, {'K', 'O'}},
    {0x91, {'S', 'A'}},
    {0x92, {'s', 'I'}},
    {0x93, {'S', 'U'}},
    {0x94, {'S', 'E'}},
    {0x95, {'S', 'O'}},
    {0x96, {'s', 'y', 'A'}},
    {0x97, {'s', 'I'}},
    {0x98, {'s', 'y', 'U'}},
    {0x99, {'s', 'y', 'E'}},
    {0x9A, {'s', 'y', 'O'}},
    {0x9B, {'H', 'A'}},
    {0x9E, {'H', 'E'}},
    {0x9F, {'H', 'O'}},
    {0xA0, {'h', 'y', 'A'}},
    {0xA1, {'h', 'I'}},
    {0xA2, {'h', 'y', 'U'}},
    {0xA3, {'H', 'E'}},
    {0xA4, {'h', 'y', 'O'}},
    {0xA5, {'F', 'A'}},
    {0xA6, {'F', 'I'}},
    {0xA7, {'F', 'U'}},
    {0xA8, {'F', 'E'}},
    {0xA9, {'F', 'O'}},
    {0xAA, {'x', 'A'}},
    {0xAB, {'x', 'I'}},
    {0xAC, {'x', 'U'}},
    {0xAD, {'x', 'E'}},
    {0xAE, {'x', 'O'}},
    {0xAF, {'t', 'y', 'A'}},
    {0xB0, {'t', 'I'}},
    {0xB1, {'t', 'y', 'U'}},
    {0xB2, {'t', 'y', 'E'}},
    {0xB3, {'t', 'y', 'O'}},
    {0xB4, {'P', 'A'}},
    {0xB5, {'p'}},
    {0xB6, {'P'}},
    {0xB7, {'P', 'E'}},
    {0xB8, {'P', 'O'}},
    {0xB9, {'p', 'y', 'A'}},
    {0xBB, {'p', 'y', 'U'}},
    {0xBD, {'p', 'y', 'O'}},
    {0xBE, {'T', 'A'}},
    {0xBF, {'T', 'I'}},
    {0xC0, {'T', 'U'}},
    {0xC1, {'T', 'E'}},
    {0xC2, {'T', 'O'}},
    {0xC5, {'t', 'y', 'U'}},
    {0xC8, {'K', 'A'}},
    {0xC9, {'k'}},
    {0xCA, {'K'}},
    {0xCB, {'K', 'E'}},
    {0xCC, {'K', 'O'}},
    {0xCD, {'k', 'y', 'A'}},
    {0xCF, {'k', 'y', 'U'}},
    {0xD1, {'k', 'y', 'O'}},
    {0xD2, {'K', 'A'}},
    {0xD3, {'K', 'I'}},
    {0xD5, {'K', 'E'}},
    {0xD6, {'K', 'O'}},
    {0xD7, {'S', 'A'}},
    {0xD8, {'s'}},
    {0xD9, {'S'}},
    {0xDA, {'S', 'E'}},
    {0xDB, {'S', 'O'}},
    {0xDC, {'s', 'y', 'A'}},
    {0xDD, {'s'}},
    {0xDE, {'c'}},
    {0xDF, {'s', 'y', 'E'}},
    {0xE0, {'s', 'y', 'O'}},
    {0xE1, {'H', 'A'}},
    {0xE4, {'H', 'E'}},
    {0xE5, {'H', 'O'}},
    {0xE6, {'h', 'y', 'A'}},
    {0xE7, {'h'}},
    {0xE8, {'h', 'y', 'U'}},
    {0xE9, {'H', 'E'}},
    {0xEA, {'h', 'y', 'O'}},
    {0xEB, {'F', 'A'}},
    {0xEC, {'F', 'I'}},
    {0xED, {'F'}},
    {0xEE, {'F', 'E'}},
    {0xEF, {'F', 'O'}},
    {0xF0, {'x', 'A'}},
    {0xF1, {'x', 'I'}},
    {0xF2, {'x'}},
    {0xF3, {'x', 'E'}},
    {0xF4, {'x', 'O'}},
    {0xF5, {'t', 'y', 'A'}},
    {0xF6, {'t'}},
    {0xF7, {'t', 'y', 'U'}},
    {0xF8, {'t', 'y', 'E'}},
    {0xF9, {'t', 'y', 'O'}},
};

static const std::unordered_map<char, std::vector<std::uint8_t>> kAsciiReadings = {
    {' ', {0xD9, 0x71, 0x02, 0x93}},
    {'!', {0x82, 0x04, 0x78, 0x04, 0xA7}},
    {'"', {0x4B, 0x43, 0x16, 0x86, 0x02, 0x7C}},
    {'#', {0x96, 0x02, 0x70}},
    {'$', {0x4F, 0x16}},
    {'%', {0x6E, 0x02, 0x94, 0x04, 0x7C}},
    {'&', {0x05, 0x04, 0x4F}},
    {'\'', {0x05, 0x72, 0xD9, 0x7C, 0x18, 0xA6}},
    {'(', {0xA1, 0x4B, 0x15, 0x82, 0x03, 0x86}},
    {')', {0x1F, 0x33, 0x82, 0x03, 0x86}},
    {'*', {0x05, 0xD9, 0x78, 0x15, 0xD9, 0x84}},
    {'+', {0x70, 0x14, 0x93}},
    {',', {0x86, 0x04, 0x1E}},
    {'-', {0x1E, 0x06, 0x28, 0x93}},
    {'.', {0x6F, 0x15, 0x09, 0x4F}},
    {'/', {0x93, 0x14, 0x03, 0x98}},
    {'0', {0x67, 0x18}},
    {'1', {0x06, 0xB0}},
    {'2', {0x29, 0x02}},
    {'3', {0x91, 0x04}},
    {'4', {0x0E, 0x04}},
    {'5', {0x59, 0x02}},
    {'6', {0x18, 0x84}},
    {'7', {0x28, 0x28}},
    {'8', {0x9B, 0xB0}},
    {'9', {0x89, 0x02}},
    {':', {0x86, 0x18, 0x04}},
    {';', {0x94, 0x1F, 0x86, 0x18, 0x04}},
    {'<', {0x93, 0x22, 0x02, 0x16}},
    {'=', {0x06, 0x86, 0x02, 0x16}},
    {'>', {0x14, 0x02, 0x6A}},
    {'?', {0x56, 0x22, 0x04, 0xA7}},
    {'@', {0x05, 0x03, 0x7C, 0x1E, 0x02, 0x84}},
    {'A', {0x08, 0x06}},
    {'B', {0x42, 0x02}},
    {'C', {0x97, 0x02}},
    {'D', {0x4C, 0x02}},
    {'E', {0x06, 0x02}},
    {'F', {0x08, 0xA7}},
    {'G', {0x6A, 0x02}},
    {'H', {0x08, 0x03, 0xB0}},
    {'I', {0x05, 0x06}},
    {'J', {0x6C, 0x02}},
    {'K', {0x85, 0x02}},
    {'L', {0x08, 0x16}},
    {'M', {0x08, 0x20}},
    {'N', {0x08, 0x2A}},
    {'O', {0x09, 0x02}},
    {'P', {0x6F, 0x02}},
    {'Q', {0x89, 0x02}},
    {'R', {0x05, 0x02, 0x16}},
    {'S', {0x08, 0x93}},
    {'T', {0x79, 0x02}},
    {'U', {0x0C, 0x02}},
    {'V', {0x43, 0x06}},
    {'W', {0x4B, 0x43, 0x1B, 0x02}},
    {'X', {0x08, 0x03, 0xCA, 0x93}},
    {'Y', {0x0F, 0x06}},
    {'Z', {0x67, 0x03, 0x7C}},
    {'[', {0xA1, 0x4B, 0x15, 0x82, 0x03, 0x86}},
    {'\\', {0x08, 0x04}},
    {']', {0x1F, 0x33, 0x82, 0x03, 0x86}},
    {'^', {0x97, 0x16, 0x86, 0x04, 0xA7, 0x17, 0xCA, 0x93}},
    {'_', {0x05, 0x04, 0x4B, 0x02, 0x14, 0x06, 0x04}},
    {'a', {0x08, 0x06}},
    {'b', {0x42, 0x02}},
    {'c', {0x97, 0x02}},
    {'d', {0x4C, 0x02}},
    {'e', {0x06, 0x02}},
    {'f', {0x08, 0xA7}},
    {'g', {0x6A, 0x02}},
    {'h', {0x08, 0x03, 0xB0}},
    {'i', {0x05, 0x06}},
    {'j', {0x6C, 0x02}},
    {'k', {0x85, 0x02}},
    {'l', {0x08, 0x16}},
    {'m', {0x08, 0x20}},
    {'n', {0x08, 0x2A}},
    {'o', {0x09, 0x02}},
    {'p', {0x6F, 0x02}},
    {'q', {0x89, 0x02}},
    {'r', {0x05, 0x02, 0x16}},
    {'s', {0x08, 0x93}},
    {'t', {0x79, 0x02}},
    {'u', {0x0C, 0x02}},
    {'v', {0x43, 0x06}},
    {'w', {0x4B, 0x43, 0x1B, 0x02}},
    {'x', {0x08, 0x03, 0xCA, 0x93}},
    {'y', {0x0F, 0x06}},
    {'z', {0x67, 0x03, 0x7C}},
    {'{', {0xA1, 0x4B, 0x15, 0x82, 0x03, 0x86}},
    {'}', {0x1F, 0x33, 0x82, 0x03, 0x86}},
    {'~', {0x82, 0x14}},
};

static const NumericRule kNumericRules[] = {
    {U"\u516D\u767E", {0x18, 0x03, 0x73, 0x84}},
    {U"\u516D\u5343", {0x18, 0x84, 0x94, 0x04}},
    {U"\u516D\u5341", {0x18, 0x84, 0x6B, 0x02}},
    {U"\u516D", {0x18, 0x84}},
    {U"\u96F6", {0x17, 0x06}},
    {U"\u4E07", {0x1E, 0x04}},
    {U"\u767E", {0xA0, 0x84}},
    {U"\u516B\u767E", {0x9B, 0x03, 0x73, 0x84}},
    {U"\u516B\u5343", {0x9B, 0x03, 0x94, 0x04}},
    {U"\u516B\u5341", {0x9B, 0xB0, 0x6B, 0x02}},
    {U"\u516B", {0x9B, 0xB0}},
    {U"\u4E8C\u767E", {0x29, 0xA0, 0x84}},
    {U"\u4E8C\u5343", {0x29, 0x94, 0x04}},
    {U"\u4E8C\u5341", {0x29, 0x6B, 0x02}},
    {U"\u4E8C", {0x29}},
    {U"\u5146", {0xB3, 0x02}},
    {U"\u5343", {0x94, 0x04}},
    {U"\u6570\u767E", {0x93, 0x02, 0xA0, 0x84}},
    {U"\u6570\u5343", {0x93, 0x02, 0x94, 0x04}},
    {U"\u6570\u5341", {0x93, 0x02, 0x6B, 0x02}},
    {U"\u6570", {0x93, 0x02}},
    {U"\u5341", {0x6B, 0x02}},
    {U"\u4E03\u767E", {0x28, 0x28, 0xA0, 0x84}},
    {U"\u4E03\u5343", {0x28, 0x28, 0x94, 0x04}},
    {U"\u4E03\u5341", {0x28, 0x28, 0x6B, 0x02}},
    {U"\u4E03", {0x28, 0x28}},
    {U"\u56DB\u767E", {0x0E, 0x04, 0xA0, 0x84}},
    {U"\u56DB\u5343", {0x0E, 0x04, 0x94, 0x04}},
    {U"\u56DB\u5341", {0x0E, 0x04, 0x6B, 0x02}},
    {U"\u56DB", {0x0E, 0x04}},
    {U"\u4E09\u767E", {0x91, 0x04, 0x46, 0x84}},
    {U"\u4E09\u5343", {0x91, 0x04, 0x67, 0x04}},
    {U"\u4E09\u5341", {0x91, 0x04, 0x6B, 0x02}},
    {U"\u4E09", {0x91, 0x04}},
    {U"\u4E94\u767E", {0x59, 0xA0, 0x84}},
    {U"\u4E94\u5343", {0x59, 0x94, 0x04}},
    {U"\u4E94\u5341", {0x59, 0x6B, 0x02}},
    {U"\u4E94", {0x59}},
    {U"\u4E5D\u767E", {0x89, 0x02, 0xA0, 0x84}},
    {U"\u4E5D\u5343", {0x89, 0x02, 0x94, 0x04}},
    {U"\u4E5D\u5341", {0x89, 0x02, 0x6B, 0x02}},
    {U"\u4E5D", {0x89, 0x02}},
    {U"\u5E7E\u767E", {0x06, 0xCA, 0xA0, 0x84}},
    {U"\u5E7E\u5343", {0x06, 0xCA, 0x94, 0x04}},
    {U"\u5E7E\u5341", {0x06, 0x84, 0x6B, 0x02}},
    {U"\u5E7E", {0x06, 0x84}},
    {U"\u4F55\u767E", {0x28, 0x04, 0x46, 0x84}},
    {U"\u4F55\u5343", {0x28, 0x04, 0x67, 0x04}},
    {U"\u4F55\u5341", {0x28, 0x04, 0x6B, 0x02}},
    {U"\u4F55", {0x28, 0x04}},
    {U"\u5104", {0x09, 0x84}},
    {U"\u4E00\u5343", {0x06, 0x03, 0x94, 0x04}},
    {U"\u4E00", {0x06, 0xB0}},
    {U"\u25EF", {0x67, 0x18}},
    {U"\u3007", {0x67, 0x18}},
};

static const std::array<const char32_t*, 10> kThousands = {
    U"", U"\u5343", U"\u4E8C\u5343", U"\u4E09\u5343", U"\u56DB\u5343", U"\u4E94\u5343", U"\u516D\u5343", U"\u4E03\u5343", U"\u516B\u5343", U"\u4E5D\u5343"
};

static const std::array<const char32_t*, 10> kHundreds = {
    U"", U"\u767E", U"\u4E8C\u767E", U"\u4E09\u767E", U"\u56DB\u767E", U"\u4E94\u767E", U"\u516D\u767E", U"\u4E03\u767E", U"\u516B\u767E", U"\u4E5D\u767E"
};

static const std::array<const char32_t*, 10> kTens = {
    U"", U"\u5341", U"\u4E8C\u5341", U"\u4E09\u5341", U"\u56DB\u5341", U"\u4E94\u5341", U"\u516D\u5341", U"\u4E03\u5341", U"\u516B\u5341", U"\u4E5D\u5341"
};

static const std::array<const char32_t*, 10> kOnes = {
    U"\u3007", U"\u4E00", U"\u4E8C", U"\u4E09", U"\u56DB", U"\u4E94", U"\u516D", U"\u4E03", U"\u516B", U"\u4E5D"
};


U32 decode_utf8(std::string_view input) {
    U32 out;
    for (std::size_t i = 0; i < input.size();) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        char32_t cp = 0;
        std::size_t n = 0;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
        else throw std::runtime_error("Input is not valid UTF-8.");
        if (i + n > input.size()) throw std::runtime_error("Input is not valid UTF-8.");
        for (std::size_t j = 1; j < n; ++j) {
            unsigned char t = static_cast<unsigned char>(input[i + j]);
            if ((t & 0xC0) != 0x80) throw std::runtime_error("Input is not valid UTF-8.");
            cp = (cp << 6) | (t & 0x3F);
        }
        if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
            (n == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF)) {
            throw std::runtime_error("Input is not valid UTF-8.");
        }
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::string encode_utf8(char32_t cp) {
    std::string out;
    if (cp <= 0x7F) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

char32_t compatibility_char(char32_t ch) {
    if (ch == 0x3000) return U' ';
    if (ch >= 0xFF01 && ch <= 0xFF5E) return ch - 0xFEE0;
    return ch;
}

char32_t to_katakana(char32_t ch) {
    ch = compatibility_char(ch);
    if (ch >= U'ぁ' && ch <= U'ゖ') return ch + 0x60;
    if (ch == U'ゔ') return U'ヴ';
    return ch;
}

bool whitespace(char32_t ch) {
    ch = compatibility_char(ch);
    return ch == U' ' || ch == U'\t' || ch == U'\n' || ch == U'\r';
}

bool long_mark(char32_t ch) { return ch == U'ー' || ch == U'−' || ch == U'〜'; }
bool ascii_digit(char32_t ch) { return ch >= U'0' && ch <= U'9'; }
bool star_onset(char phone) { return phone=='P'||phone=='p'||phone=='T'||phone=='t'||phone=='K'||phone=='k'||phone=='x'; }
bool is_long_code(std::uint8_t code) { return code == 0x02 || code == 0xFB || code == 0xFC; }

bool ignorable(char32_t ch) {
    static const U32 chars = U"、。，．！？!?・「」『』【】（）()";
    return chars.find(ch) != U32::npos;
}

bool match_rule(const U32& text, std::size_t pos, const char32_t* pattern) {
    U32 p(pattern);
    if (pos + p.size() > text.size()) return false;
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (to_katakana(text[pos + i]) != p[i]) return false;
    }
    return true;
}

bool match_mora(const U32& text, std::size_t pos, std::uint8_t& code, std::size_t& consumed) {
    for (const auto& rule : kMoraRules) {
        U32 p(rule.text);
        if (match_rule(text, pos, rule.text)) {
            code = rule.code;
            consumed = p.size();
            return true;
        }
    }
    return false;
}

bool lengthen_last(Mora& mora) {
    for (std::size_t i = mora.phones.size(); i-- > 0;) {
        switch (mora.phones[i]) {
            case 'A': mora.phones[i] = 'a'; return true;
            case 'I': mora.phones[i] = 'i'; return true;
            case 'U': mora.phones[i] = 'u'; return true;
            case 'E': mora.phones[i] = 'e'; return true;
            case 'O': mora.phones[i] = 'o'; return true;
        }
    }
    return false;
}

void append_codes(const std::vector<std::uint8_t>& codes, std::vector<Mora>& morae,
                  std::optional<std::uint8_t>& previous) {
    for (std::uint8_t code : codes) {
        if (is_long_code(code)) {
            if (morae.empty() || !lengthen_last(morae.back()))
                throw std::runtime_error("Loose long-vowel marker.");
            morae.back().weight += 1;
            previous = code;
            continue;
        }
        auto found = kCodeToVrs.find(code);
        if (found == kCodeToVrs.end())
            throw std::runtime_error(" pronunciation code has no VRS spelling.");
        auto phones = found->second;
        if (previous && *previous != 0x03 && !phones.empty() && star_onset(phones.front()))
            phones.insert(phones.begin(), '*');
        morae.push_back({std::move(phones), 1, code});
        previous = code;
    }
}

bool number_start(const U32& text, std::size_t pos) {
    char32_t ch = compatibility_char(text[pos]);
    if (ascii_digit(ch)) return true;
    return (ch == U'+' || ch == U'-') && pos + 1 < text.size() && ascii_digit(compatibility_char(text[pos+1]));
}

std::size_t scan_number(const U32& text, std::size_t pos) {
    std::size_t i = pos;
    char32_t ch = compatibility_char(text[i]);
    if (ch == U'+' || ch == U'-') ++i;
    while (i < text.size()) {
        ch = compatibility_char(text[i]);
        if (!(ascii_digit(ch) || ch == U',' || ch == U'.')) break;
        ++i;
    }
    return i;
}

std::string ascii_from_u32(const U32& s) {
    std::string out;
    for (char32_t cp : s) {
        cp = compatibility_char(cp);
        if (cp > 0x7F) return {};
        out.push_back(static_cast<char>(cp));
    }
    return out;
}

bool valid_number(const std::string& token) {
    std::size_t p = 0;
    if (p < token.size() && (token[p]=='+' || token[p]=='-')) ++p;
    std::size_t dot = token.find('.', p);
    std::string integer = token.substr(p, dot == std::string::npos ? std::string::npos : dot-p);
    if (integer.empty()) return false;
    if (dot != std::string::npos) {
        if (dot + 1 >= token.size()) return false;
        for (std::size_t i=dot+1;i<token.size();++i) if (!std::isdigit(static_cast<unsigned char>(token[i]))) return false;
    }
    auto comma = integer.find(',');
    if (comma == std::string::npos) {
        return std::all_of(integer.begin(), integer.end(), [](unsigned char c){ return std::isdigit(c); });
    }
    std::size_t start=0, group=0;
    while (true) {
        std::size_t end=integer.find(',', start);
        std::string part=integer.substr(start,end==std::string::npos?std::string::npos:end-start);
        if (part.empty() || !std::all_of(part.begin(),part.end(),[](unsigned char c){return std::isdigit(c);})) return false;
        if (group==0) { if (part.size()>3) return false; }
        else if (part.size()!=3) return false;
        ++group;
        if (end==std::string::npos) break;
        start=end+1;
    }
    return group >= 2;
}

U32 format_four(int value) {
    if (value == 0) return U32(kOnes[0]);
    int th=value/1000; value%=1000;
    int hu=value/100; value%=100;
    int te=value/10; int on=value%10;
    U32 out=kThousands[th]; out+=kHundreds[hu]; out+=kTens[te]; if(on>0) out+=kOnes[on]; return out;
}

U32 format_japanese_integer(std::string digits) {
    auto nz=digits.find_first_not_of('0');
    if(nz==std::string::npos) return U32(kOnes[0]);
    digits=digits.substr(nz);
    U32 out;
    int len=static_cast<int>(digits.size()), first=len%4; if(first==0) first=4;
    int pos=0, groups=(len+3)/4;
    for(int g=groups-1; g>=0; --g) {
        int take=pos==0?first:4;
        int value=std::stoi(digits.substr(pos,take)); pos+=take;
        if(value==0) continue;
        out += format_four(value);
        if(g==3) out+=U"兆"; else if(g==2) out+=U"億"; else if(g==1) out+=U"万";
    }
    return out;
}

bool append_numeric_kanji(const U32& text, std::vector<std::uint8_t>& output) {
    for(std::size_t pos=0; pos<text.size();) {
        bool matched=false;
        for(const auto& rule:kNumericRules) {
            U32 p(rule.text);
            if(pos+p.size()<=text.size() && text.compare(pos,p.size(),p)==0) {
                output.insert(output.end(),rule.codes.begin(),rule.codes.end()); pos+=p.size(); matched=true; break;
            }
        }
        if(!matched) return false;
    }
    return true;
}

void number_to_codes(const std::string& token, std::vector<std::uint8_t>& output) {
    std::size_t p=0;
    if(token[p]=='+'){ auto v=std::vector<std::uint8_t>{0x70,0x14,0x93}; output.insert(output.end(),v.begin(),v.end()); ++p; }
    else if(token[p]=='-'){ auto v=std::vector<std::uint8_t>{0x1E,0x06,0x28,0x93}; output.insert(output.end(),v.begin(),v.end()); ++p; }
    std::string u=token.substr(p);
    std::size_t dot=u.find('.');
    std::string integer=u.substr(0,dot);
    integer.erase(std::remove(integer.begin(),integer.end(),','),integer.end());
    if((integer.size()>1 && integer[0]=='0') || integer.size()>16) {
        for(char d:integer){ auto it=kAsciiReadings.find(d); output.insert(output.end(),it->second.begin(),it->second.end()); }
    } else {
        U32 kanji=format_japanese_integer(integer);
        if(!append_numeric_kanji(kanji,output)) throw std::runtime_error("Could not parse retail numeric form.");
    }
    if(dot!=std::string::npos) {
        output.push_back(0x7B); output.push_back(0x04);
        for(char d:u.substr(dot+1)){ auto it=kAsciiReadings.find(d); output.insert(output.end(),it->second.begin(),it->second.end()); }
    }
}

} // namespace

std::vector<Mora> analyze(std::string_view utf8) {
    U32 text=decode_utf8(utf8);
    std::vector<Mora> result;
    std::vector<std::uint8_t> codes;
    std::optional<std::uint8_t> previous;
    for(std::size_t i=0;i<text.size();) {
        char32_t ch=compatibility_char(text[i]);
        if(whitespace(ch)) {
            append_codes(codes,result,previous); codes.clear(); previous.reset(); ++i; continue;
        }
        if(long_mark(ch)) {
            if(!codes.empty()) {
                std::uint8_t prior=codes.back();
                if(prior<=0x04){ if(prior==0x04) codes.push_back(0x04); }
                else codes.push_back(0x02);
            }
            ++i; continue;
        }
        if((ch==U'っ'||ch==U'ッ') && codes.empty()){ ++i; continue; }
        if(number_start(text,i)) {
            auto end=scan_number(text,i); U32 part=text.substr(i,end-i); std::string token=ascii_from_u32(part);
            if(valid_number(token)){ number_to_codes(token,codes); i=end; continue; }
        }
        std::uint8_t code=0; std::size_t consumed=0;
        if(match_mora(text,i,code,consumed)){ codes.push_back(code); i+=consumed; continue; }
        if(ch<=0x7F) {
            auto it=kAsciiReadings.find(static_cast<char>(ch));
            if(it!=kAsciiReadings.end()){ codes.insert(codes.end(),it->second.begin(),it->second.end()); ++i; continue; }
        }
        if(ignorable(ch)){ ++i; continue; }
        throw std::runtime_error("No recovered reading for '"+encode_utf8(ch)+"'.");
    }
    append_codes(codes,result,previous);
    if(result.empty()) throw std::runtime_error("The phrase contains no readable text.");
    return result;
}

} // namespace mojib_text
