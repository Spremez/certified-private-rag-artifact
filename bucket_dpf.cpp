#include <immintrin.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {
using Block = __m128i;
struct Node { Block seed; unsigned control; };
struct Children { Node branch[2]; };

template<int Round>
inline Block next_key(Block key) {
    Block assist = _mm_aeskeygenassist_si128(key, Round);
    assist = _mm_shuffle_epi32(assist, 0xff);
    Block shifted = _mm_slli_si128(key, 4);
    key = _mm_xor_si128(key, shifted);
    shifted = _mm_slli_si128(shifted, 4);
    key = _mm_xor_si128(key, shifted);
    return _mm_xor_si128(assist, _mm_xor_si128(key, _mm_slli_si128(shifted, 4)));
}

template<int Round, bool Last = false>
inline void encrypt_round(Block& key, Block& left, Block& right, Block& flags) {
    key = next_key<Round>(key);
    if constexpr (Last) {
        left = _mm_aesenclast_si128(left, key);
        right = _mm_aesenclast_si128(right, key);
        flags = _mm_aesenclast_si128(flags, key);
    } else {
        left = _mm_aesenc_si128(left, key);
        right = _mm_aesenc_si128(right, key);
        flags = _mm_aesenc_si128(flags, key);
    }
}

Children expand(Block key) {
    Block left = key, right = _mm_xor_si128(key, _mm_set_epi32(0, 0, 0, 1));
    Block flags = _mm_xor_si128(key, _mm_set_epi32(0, 0, 0, 2));
    encrypt_round<1>(key,left,right,flags); encrypt_round<2>(key,left,right,flags);
    encrypt_round<4>(key,left,right,flags); encrypt_round<8>(key,left,right,flags);
    encrypt_round<16>(key,left,right,flags); encrypt_round<32>(key,left,right,flags);
    encrypt_round<64>(key,left,right,flags); encrypt_round<128>(key,left,right,flags);
    encrypt_round<27>(key,left,right,flags); encrypt_round<54,true>(key,left,right,flags);
    unsigned bits = static_cast<unsigned>(_mm_cvtsi128_si32(flags));
    return {{{left,bits & 1U},{right,(bits >> 1) & 1U}}};
}
Block load(const uint8_t* bytes) { return _mm_loadu_si128(reinterpret_cast<const Block*>(bytes)); }
void store(uint8_t* bytes, Block value) { _mm_storeu_si128(reinterpret_cast<Block*>(bytes), value); }
Node correct(Node child, unsigned active, Block seed, unsigned control) {
    if (active) { child.seed = _mm_xor_si128(child.seed,seed); child.control ^= control; }
    return child;
}
}

extern "C" int bd_gen(unsigned depth, unsigned index, const uint8_t* entropy, uint8_t* left, uint8_t* right) {
    if (depth > 24 || index >= (1U << depth)) return -1;
    Node first{load(entropy),0}, second{load(entropy+16),1};
    store(left,first.seed);
    store(right,second.seed);
    for (unsigned level=0; level<depth; ++level) {
        const unsigned keep = (index >> (depth-level-1)) & 1U;
        const auto first_children = expand(first.seed), second_children = expand(second.seed);
        const Block word = _mm_xor_si128(first_children.branch[1-keep].seed,second_children.branch[1-keep].seed);
        const unsigned controls[2] = {
            first_children.branch[0].control ^ second_children.branch[0].control ^ (1U-keep),
            first_children.branch[1].control ^ second_children.branch[1].control ^ keep};
        const unsigned offset = 16+17*level;
        store(left+offset,word);
        left[offset+16] = static_cast<uint8_t>(controls[0] | (controls[1]<<1));
        std::memcpy(right+offset,left+offset,17);
        first = correct(first_children.branch[keep],first.control,word,controls[keep]);
        second = correct(second_children.branch[keep],second.control,word,controls[keep]);
    }
    return 0;
}

extern "C" int bd_eval(unsigned depth, unsigned role, const uint8_t* key, unsigned positions, uint8_t* bits) {
    if (depth > 24 || role > 1 || !positions || positions > (1U<<depth)) return -1;
    std::memset(bits,0,(positions+7)/8);
    std::vector<Node> nodes{{load(key),role}};
    for (unsigned level=0; level<depth; ++level) {
        const uint8_t* correction = key+16+17*level;
        if (correction[16] > 3) return -1;
        const unsigned remaining = depth-1-level;
        const unsigned live = ((positions-1)>>remaining)+1;
        std::vector<Node> next(live);
        for (unsigned parent=0; parent<nodes.size(); ++parent) {
            const auto children = expand(nodes[parent].seed);
            for (unsigned branch=0; branch<2 && 2*parent+branch<live; ++branch) {
                next[2*parent+branch] = correct(children.branch[branch],nodes[parent].control,
                                                load(correction),(correction[16]>>branch)&1U);
            }
        }
        nodes.swap(next);
    }
    for (unsigned pos=0; pos<positions; ++pos)
        bits[pos/8] |= static_cast<uint8_t>(nodes[pos].control << (pos%8));
    return 0;
}
