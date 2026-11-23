#include <seal/seal.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t degree = 8192;
constexpr std::uint64_t prime = 2147483647;
thread_local std::string last_error;

enum class Layout { power_two, aligned, separate };

seal::EncryptionParameters parameters() {
    seal::EncryptionParameters params(seal::scheme_type::bfv);
    params.set_poly_modulus_degree(degree);
    params.set_coeff_modulus(seal::CoeffModulus::Create(degree, {56, 55, 56}));
    params.set_plain_modulus(prime);
    return params;
}

struct Engine {
    seal::SEALContext context{parameters(), true, seal::sec_level_type::tc128};
    seal::Evaluator evaluator{context};
    std::unique_ptr<seal::Encryptor> encryptor;
    std::unique_ptr<seal::Decryptor> decryptor;
    std::unique_ptr<seal::KeyGenerator> keygen;
    seal::GaloisKeys rotations;
    bool packed = false;
    bool live_leaves = false;
    int offline_budget = -1;
    Layout layout = Layout::power_two;
    explicit Engine(bool client) {
        if (!context.parameters_set()) throw std::runtime_error(context.parameter_error_message());
        if (client) {
            keygen = std::make_unique<seal::KeyGenerator>(context);
            encryptor = std::make_unique<seal::Encryptor>(context, keygen->secret_key());
            decryptor = std::make_unique<seal::Decryptor>(context, keygen->secret_key());
        }
    }
};

void copy_result(const std::string& data, void** out, std::size_t* size) {
    void* allocation = std::malloc(data.size());
    if (!allocation) throw std::bad_alloc();
    std::memcpy(allocation, data.data(), data.size());
    *size = data.size();
    *out = allocation;
}

std::size_t power_of_two(std::size_t value) {
    std::size_t power = 1;
    while (power < value) power *= 2;
    return power;
}

struct Packing {
    std::size_t stride, capacity, blocks, leaves;
};

Packing packing(std::size_t count, std::size_t dim, Layout layout) {
    if (!count || count > degree || !dim || dim > degree) throw std::invalid_argument("packing shape");
    std::size_t stride = power_of_two(dim);
    if (layout == Layout::separate) stride = dim;
    if (layout == Layout::aligned) {
        for (std::size_t alignment = 1; alignment <= degree; alignment *= 2) {
            stride = ((dim + alignment - 1) / alignment) * alignment;
            if (alignment * (degree / stride) >= count) break;
        }
    }
    auto capacity = degree / stride;
    auto blocks = (count + capacity - 1) / capacity;
    return {stride, capacity, blocks, layout == Layout::separate ? 1 : power_of_two(blocks)};
}

std::uint64_t inverse_leaf_weight(std::size_t leaves) {
    std::uint64_t inverse = 1;
    for (; leaves > 1; leaves /= 2) inverse = (inverse + (inverse % 2) * prime) / 2;
    return inverse;
}

void write_word(std::ostream& output, std::uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) output.put(static_cast<char>(value >> (8*byte)));
}

std::uint64_t read_word(std::istream& input) {
    std::uint64_t value = 0;
    for (unsigned byte = 0; byte < 8; ++byte) {
        int part = input.get();
        if (part == std::char_traits<char>::eof()) throw std::invalid_argument("short packed BFV reply");
        value |= std::uint64_t(part) << (8*byte);
    }
    return value;
}

std::size_t coordinate(std::size_t lane, const Packing& shape) {
    return (lane % shape.capacity) * shape.stride + lane / shape.capacity;
}

void serialize_selected(Engine* engine, seal::Ciphertext& ciphertext, std::ostream& output,
                        std::size_t count, const Packing& shape) {
    if (!ciphertext.size()) {
        for (std::size_t pos = 0; pos < count + degree; ++pos) write_word(output, 0);
        return;
    }
    engine->evaluator.mod_switch_to_inplace(ciphertext, engine->context.last_parms_id());
    if (engine->decryptor) {
        int budget = engine->decryptor->invariant_noise_budget(ciphertext);
        engine->offline_budget = engine->offline_budget < 0 ? budget : std::min(engine->offline_budget,budget);
    }
    if (ciphertext.is_ntt_form() || ciphertext.size() != 2 || ciphertext.coeff_modulus_size() != 1)
        throw std::runtime_error("unexpected terminal BFV representation");
    for (std::size_t pos = 0; pos < count; ++pos) write_word(output, ciphertext.data(0)[coordinate(pos, shape)]);
    for (std::size_t pos = 0; pos < degree; ++pos) write_word(output, ciphertext.data(1)[pos]);
}

seal::Plaintext decrypt_selected(Engine* engine, std::istream& input, std::size_t count, const Packing& shape) {
    const auto maximum = engine->context.last_context_data()->parms().coeff_modulus()[0].value();
    auto coefficient = [&] {
        auto value = read_word(input);
        if (value >= maximum) throw std::invalid_argument("noncanonical BFV coefficient");
        return value;
    };
    seal::Ciphertext ciphertext(engine->context, engine->context.last_parms_id());
    ciphertext.resize(engine->context, engine->context.last_parms_id(), 2);
    std::fill_n(ciphertext.data(0), degree, 0);
    for (std::size_t pos = 0; pos < count; ++pos) ciphertext.data(0)[coordinate(pos, shape)] = coefficient();
    for (std::size_t pos = 0; pos < degree; ++pos) ciphertext.data(1)[pos] = coefficient();
    seal::Plaintext decoded;
    engine->decryptor->decrypt(ciphertext, decoded);
    return decoded;
}

template<typename Operation>
int checked(Operation operation) {
    try { operation(); return 0; }
    catch (const std::exception& exc) { last_error = exc.what(); return -1; }
}
}

extern "C" {
const char* bf_error() { return last_error.c_str(); }
int bf_offline_budget(Engine* engine) { return engine->offline_budget; }
void* bf_engine(int client) {
    try { return new Engine(client != 0); }
    catch (const std::exception& exc) { last_error = exc.what(); return nullptr; }
}
void bf_destroy(Engine* engine) { delete engine; }
void bf_free(void* data) { std::free(data); }
void bf_set_live_leaves(Engine* engine) { engine->live_leaves = true; }

int bf_set_layout(Engine* engine, int code) {
    return checked([&] {
        if (code < 0 || code > 2) throw std::invalid_argument("unknown reply layout");
        engine->layout = static_cast<Layout>(code);
        if (engine->layout == Layout::separate) engine->packed = true;
    });
}

int bf_shape(Engine* engine, std::size_t count, std::size_t dim, std::size_t* output) {
    return checked([&] {
        const auto shape = packing(count, dim, engine->layout);
        output[0] = shape.stride;
        output[1] = shape.capacity;
        output[2] = shape.blocks;
        output[3] = shape.leaves;
    });
}

int bf_setup(Engine* engine, std::size_t count, std::size_t dim, void** out, std::size_t* size) {
    return checked([&] {
        if (!engine->keygen || !dim || dim > degree || !count || count > degree) throw std::invalid_argument("packing shape");
        auto chunks = packing(count, dim, engine->layout).leaves;
        std::vector<std::uint32_t> elements;
        for (std::size_t shift = 1; shift < chunks; shift *= 2) {
            elements.push_back(static_cast<std::uint32_t>(1 + degree/shift));
        }
        std::ostringstream output(std::ios::binary);
        engine->keygen->create_galois_keys(elements).save(output, seal::compr_mode_type::none);
        engine->packed = true;
        copy_result(output.str(), out, size);
    });
}

int bf_install(Engine* engine, const char* bytes, std::size_t length) {
    return checked([&] {
        std::istringstream input(std::string(bytes, length), std::ios::binary);
        engine->rotations.load(engine->context, input);
        engine->packed = true;
    });
}

int bf_encrypt(Engine* engine, const std::int16_t* query, std::size_t dim, void** out, std::size_t* size) {
    return checked([&] {
        if (!engine->encryptor || !dim || dim > degree) throw std::invalid_argument("query dimensions or role");
        seal::Plaintext polynomial(engine->packed ? degree : dim);
        for (std::size_t col = 0; col < dim; ++col) {
            int value = engine->packed && col ? -static_cast<int>(query[col]) : query[col];
            auto index = engine->packed ? (degree-col)%degree : dim-1-col;
            polynomial[index] = value < 0 ? static_cast<std::int64_t>(prime) + value : value;
        }
        std::ostringstream stream(std::ios::binary);
        engine->encryptor->encrypt_symmetric(polynomial).save(stream, seal::compr_mode_type::none);
        copy_result(stream.str(), out, size);
    });
}

int bf_scores(Engine* engine, const char* bytes, std::size_t length, const std::uint32_t* rows,
              std::size_t count, std::size_t dim, void** out, std::size_t* size) {
    return checked([&] {
        if (!count || !dim || dim > degree) throw std::invalid_argument("score dimensions");
        std::istringstream input(std::string(bytes, length), std::ios::binary);
        seal::Ciphertext encrypted;
        encrypted.load(engine->context, input);
        if (input.peek() != std::char_traits<char>::eof()) throw std::invalid_argument("trailing query bytes");
        std::ostringstream output(std::ios::binary);
        auto stride = engine->packed ? packing(count, dim, engine->layout).stride : dim;
        auto capacity = degree / stride;
        std::vector<seal::Ciphertext> blocks;
        for (std::size_t first = 0; first < count; first += capacity) {
            auto used = std::min(capacity, count - first);
            seal::Plaintext coefficients(used*stride);
            for (std::size_t slot=0; slot<used; ++slot) {
                std::copy_n(rows + (first+slot)*dim, dim, coefficients.data()+slot*stride);
            }
            seal::Ciphertext scores;
            if (!engine->packed || !coefficients.is_zero()) engine->evaluator.multiply_plain(encrypted, coefficients, scores);
            if (engine->layout == Layout::separate) {
                serialize_selected(engine, scores, output, used, {stride, capacity, 1, 1});
            } else if (engine->packed) blocks.push_back(std::move(scores));
            else {
                engine->evaluator.mod_switch_to_inplace(scores, engine->context.last_parms_id());
                scores.save(output, seal::compr_mode_type::none);
            }
        }
        if (engine->packed && engine->layout != Layout::separate) {
            auto shape = packing(count, dim, engine->layout);
            auto leaves = shape.leaves;
            if (!engine->live_leaves) blocks.resize(leaves, blocks.back());
            for (auto shift=leaves/2; shift > 0; shift /= 2) {
                seal::Plaintext monomial(shift+1);
                monomial[shift]=1;
                auto pairs = std::min(shift, blocks.size() - shift);
                for (std::size_t pos=0; pos<pairs; ++pos) {
                    auto& left=blocks[pos];
                    auto& right=blocks[pos+shift];
                    seal::Ciphertext diff;
                    if (!right.size()) {
                        if (!left.size()) continue;
                        diff = left;
                    } else {
                        engine->evaluator.multiply_plain_inplace(right, monomial);
                        if (left.size()) {
                            engine->evaluator.sub(left, right, diff);
                            engine->evaluator.add_inplace(left, right);
                        } else {
                            engine->evaluator.negate(right, diff);
                            left = std::move(right);
                        }
                    }
                    engine->evaluator.apply_galois_inplace(diff, static_cast<std::uint32_t>(1+degree/shift), engine->rotations);
                    engine->evaluator.add_inplace(left, diff);
                }
            }
            serialize_selected(engine, blocks.front(), output, count, shape);
        }
        copy_result(output.str(), out, size);
    });
}

int bf_decode(Engine* engine, const char* bytes, std::size_t length, std::size_t count, std::size_t dim,
              std::uint32_t* output) {
    return checked([&] {
        if (!engine->decryptor || !count || !dim || dim > degree) throw std::invalid_argument("decode dimensions or role");
        std::istringstream input(std::string(bytes, length), std::ios::binary);
        if (engine->packed) {
            auto shape = packing(count, dim, engine->layout);
            auto replies = engine->layout == Layout::separate ? shape.blocks : 1;
            if (length != 8*(count+replies*degree)) throw std::invalid_argument("invalid packed score shape");
            if (engine->layout == Layout::separate) {
                for (std::size_t first = 0; first < count; first += shape.capacity) {
                    auto used = std::min(shape.capacity, count - first);
                    auto decoded = decrypt_selected(engine, input, used, shape);
                    for (std::size_t slot = 0; slot < used; ++slot) {
                        auto index = coordinate(slot, shape);
                        output[first+slot] = index < decoded.coeff_count() ? decoded[index] : 0;
                    }
                }
                return;
            }
            auto decoded = decrypt_selected(engine, input, count, shape);
            auto deep_inverse = inverse_leaf_weight(shape.leaves);
            auto shallow_inverse = (2 * deep_inverse) % prime;
            for (std::size_t pos=0; pos<count; ++pos) {
                auto index=coordinate(pos, shape);
                auto sibling = (pos / shape.capacity) ^ (shape.leaves / 2);
                auto inverse = engine->live_leaves && sibling >= shape.blocks ? shallow_inverse : deep_inverse;
                output[pos]=index<decoded.coeff_count() ? (decoded[index]*inverse)%prime : 0;
            }
            return;
        }
        auto capacity = degree / dim;
        for (std::size_t first = 0; first < count; first += capacity) {
            seal::Ciphertext scores;
            scores.load(engine->context, input);
            seal::Plaintext decoded;
            engine->decryptor->decrypt(scores, decoded);
            for (std::size_t slot = 0; slot < std::min(capacity, count - first); ++slot) {
                auto position = (slot + 1) * dim - 1;
                output[first + slot] = position < decoded.coeff_count() ? decoded[position] : 0;
            }
        }
        if (input.peek() != std::char_traits<char>::eof()) throw std::invalid_argument("trailing score bytes");
    });
}
}
