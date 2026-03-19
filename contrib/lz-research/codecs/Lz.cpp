// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "Lz.hpp"

#include <array>
#include <limits>
#include <queue>

#include "openzl/codecs/common/copy.h"
#include "openzl/codecs/common/count.h"
#include "openzl/codecs/common/fast_table.h"
#include "openzl/codecs/common/fast_table16.h"
#include "openzl/codecs/common/fast_tag_table.h"
#include "openzl/shared/bits.h"
#include "openzl/shared/portability.h"
#include "openzl/shared/simd_wrapper.h"
#include "openzl/shared/utils.h"
#include "openzl/shared/varint.h"
#include "openzl/zl_errors.h"

#include "CodecIDs.hpp"
#include "SmallInt.hpp"

#if ZL_ARCH_X86_64
#    include <smmintrin.h>
#    include <tmmintrin.h>
#endif

#define USE_LLVM_MCA 0

// TODO: Restrict lengths to uint16_t
// The loss in efficiency should be tiny,
// and we should gain a good amount of speed.

namespace openzl::lz {

namespace {
constexpr bool kQuartet         = false;
constexpr size_t kQuartetNum    = 4;
constexpr size_t kQuartetLength = 16;

template <typename T>
class FastVector {
   public:
    static_assert(std::is_pod<T>::value, "T must be POD");
    static_assert(
            std::is_trivially_destructible<T>::value,
            "T must be trivially destructible");

    FastVector() = default;

    void resize(size_t size)
    {
        if (size > capacity_) {
            reserve(size);
        }
        size_ = size;
    }

    void wrap(T* data, size_t size)
    {
        if (data_ != nullptr) {
            throw std::runtime_error("can't wrap() if already wrapped!");
        }
        data_     = data;
        size_     = size;
        capacity_ = size;
    }

    void reserve(size_t capacity)
    {
        if (data_ != nullptr) {
            throw std::runtime_error("can only reserve once!");
        }
        mem_      = std::make_unique_for_overwrite<T[]>(capacity);
        data_     = mem_.get();
        capacity_ = capacity;
    }

    const T* data() const
    {
        return data_;
    }

    T* data()
    {
        return data_;
    }

    size_t size() const
    {
        return size_;
    }

    const T& operator[](size_t i) const
    {
        assert(i < capacity_);
        return data_[i];
    }

    T& operator[](size_t i)
    {
        assert(i < capacity_);
        return data_[i];
    }

    void push_back(const T& value)
    {
        if (size_ == capacity_) {
            throw std::runtime_error("can't push_back() if full!");
        }
        data_[size_++] = value;
    }

    const T* begin() const
    {
        return data();
    }

    const T* end() const
    {
        return begin() + size();
    }

    void insert(const T* ptr, const T* b, const T* e)
    {
        if (ptr != end()) {
            throw std::runtime_error("can't insert() if not at end!");
        }
        if (b == e) {
            return;
        }
        size_t const size = e - b;
        if (size_ + size > capacity_) {
            throw std::runtime_error("can't insert() if full!");
        }
        memcpy(data() + size_, b, size * sizeof(T));
        size_ += size;
    }

   private:
    T* data_{ nullptr };
    std::unique_ptr<T[]> mem_{ nullptr };
    size_t size_{ 0 };
    size_t capacity_{ 0 };
};

template <typename T>
using Vector = FastVector<T>;

#if ZL_ARCH_X86_64
// Lookup table for pshufb-based packing of uint16_t elements.
// Given an 8-bit mask indicating which of 8 uint16_t elements are valid,
// returns a shuffle control vector to pack valid elements to the front.
constexpr std::array<std::array<uint8_t, 16>, 256> buildShuffleLUT()
{
    std::array<std::array<uint8_t, 16>, 256> lut{};
    for (size_t mask = 0; mask < 256; ++mask) {
        size_t outPos = 0;
        for (size_t i = 0; i < 8; ++i) {
            if (mask & (1 << i)) {
                lut[mask][outPos * 2]     = i * 2;
                lut[mask][outPos * 2 + 1] = i * 2 + 1;
                ++outPos;
            }
        }
        for (size_t j = outPos * 2; j < 16; ++j) {
            lut[mask][j] = 0x80;
        }
    }
    return lut;
}

static constexpr auto kShuffleLUT = buildShuffleLUT();

constexpr std::array<std::array<uint8_t, 16>, 256> buildDecodeShuffleLUT()
{
    std::array<std::array<uint8_t, 16>, 256> lut{};
    for (size_t mask = 0; mask < 256; ++mask) {
        size_t inPos = 0;
        for (size_t i = 0; i < 8; ++i) {
            if (mask & (1 << i)) {
                lut[mask][i * 2]     = inPos * 2;
                lut[mask][i * 2 + 1] = inPos * 2 + 1;
                ++inPos;
            } else {
                lut[mask][i * 2]     = 0x80;
                lut[mask][i * 2 + 1] = 0x80;
            }
        }
    }
    return lut;
}

static constexpr auto kDecodeShuffleLUT = buildDecodeShuffleLUT();
#endif

} // namespace

size_t constexpr kMinMatch   = 7;
size_t constexpr kTableLog   = 14;
size_t constexpr kSmallMatch = 5;
size_t constexpr kLargeMatch = 8;
size_t constexpr kNoOpOffset = 16;

inline length_t boundML(uint32_t ml)
{
    return length_t(
            std::min<uint32_t>(ml, std::numeric_limits<length_t>::max()));
}

inline length_t matchLength(ZS_FastTagTable_Entry entry, uint8_t const* ip)
{
    if constexpr (ZS_FastTagTable_kMaxMatchLen >= 12) {
        const ZL_Vec128 matchVec = ZL_Vec128_read(&entry);
        const ZL_Vec128 ipVec    = ZL_Vec128_read(ip);
        const ZL_Vec128 maskVec  = ZL_Vec128_cmp8(matchVec, ipVec);
        const uint32_t mask      = ZL_Vec128_mask8(maskVec);
        const uint32_t len       = ZL_ctz32(~mask);
        return len;
    } else {
        assert(ZS_FastTagTable_kMaxMatchLen == 8);
        const auto m = ZL_read64(&entry);
        const auto i = ZL_read64(ip);
        const auto x = m ^ i;
        if (x == 0) {
            return ZS_FastTagTable_kMaxMatchLen;
        } else {
            return ZS_nbCommonBytes(x);
        }
    }
}

inline bool valueFits(uint32_t val, uint32_t bytes)
{
    if (bytes == 4) {
        return true;
    } else if (bytes == 2) {
        return val < 65536;
    } else {
        return false;
    }
}

inline length_t
matchLength(uint8_t const* match, uint8_t const* ip, uint8_t const* iend)
{
    if (0) {
        return ZS_count(ip, match, iend);
    }
    const size_t offset = 0;
    if (0) {
        const uint64_t x = ZL_readLE64(match);
        const uint64_t y = ZL_readLE64(ip);
        const uint64_t z = x ^ y;
        if (ZL_LIKELY(z != 0)) {
            return ZL_ctz64(z) >> 3;
        }
    }
    {
        ZL_ASSERT_LE(ip + 16, iend);
        const ZL_Vec128 matchVec = ZL_Vec128_read(match + offset);
        const ZL_Vec128 ipVec    = ZL_Vec128_read(ip + offset);
        const ZL_Vec128 maskVec  = ZL_Vec128_cmp8(matchVec, ipVec);
        const uint32_t mask      = ZL_Vec128_mask8(maskVec);
        const uint32_t len       = ZL_ctz32(~mask);
        if (ZL_LIKELY(len < 16)) {
            return offset + len;
        }
    }

    uint32_t totalLength        = offset + 16;
    const uint8_t* const ilimit = iend - 16;
    while (ip + totalLength < ilimit) {
        const ZL_Vec128 matchVec = ZL_Vec128_read(match + totalLength);
        const ZL_Vec128 ipVec    = ZL_Vec128_read(ip + totalLength);
        const ZL_Vec128 maskVec  = ZL_Vec128_cmp8(matchVec, ipVec);
        const uint32_t mask      = ZL_Vec128_mask8(maskVec);
        const uint32_t length    = ZL_ctz32(~mask);
        if (length < 16) {
            return totalLength + length;
        }
        totalLength += 16;
        if (ZL_UNLIKELY(!valueFits(totalLength, sizeof(length_t)))) {
            return std::numeric_limits<length_t>::max();
        }
    }

    while (ip + totalLength < iend && ip[totalLength] == match[totalLength]) {
        ++totalLength;
    }
    return totalLength;
}

template <bool kIsIndex, size_t kLLBits, size_t kMLBits>
class Transform {
    static uint8_t constexpr kLLMask = ((1 << kLLBits) - 1);
    static uint8_t constexpr kMLMask = ((1 << kMLBits) - 1);

    static_assert(!kIsIndex, "Currently unsupported");

    template <typename T>
    static ZL_Report
    writeStream(ZL_Encoder* eictx, size_t idx, Vector<T> const& data)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

        auto stream = ZL_Encoder_createTypedStream(
                eictx, idx, data.size(), sizeof(T));
        ZL_ERR_IF_NULL(stream, allocation);
        if (data.size() > 0) {
            memcpy(ZL_Output_ptr(stream), data.data(), data.size() * sizeof(T));
        }
        ZL_ERR_IF_ERR(ZL_Output_commit(stream, data.size()));
        return ZL_returnSuccess();
    }

    template <typename T>
    static ZL_Report reserveStream(
            ZL_Encoder* eictx,
            size_t idx,
            size_t capacity,
            Vector<T>* data,
            ZL_Output** stream)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);

        *stream = ZL_Encoder_createTypedStream(eictx, idx, capacity, sizeof(T));
        ZL_ERR_IF_NULL(*stream, allocation);
        data->wrap((T*)ZL_Output_ptr(*stream), capacity);

        return ZL_returnSuccess();
    }

    ZL_Encoder* encoder_;
    std::vector<ZL_Output*> streams_;

    virtual void* reserveStream(size_t idx, size_t eltCapacity, size_t eltWidth)
    {
        auto stream = ZL_Encoder_createTypedStream(
                encoder_, idx, eltCapacity, eltWidth);
        if (stream == NULL) {
            return NULL;
        }
        streams_.resize(std::max(idx + 1, streams_.size()));
        streams_[idx] = stream;
        return ZL_Output_ptr(stream);
    }

    template <typename T>
    ZL_Report reserveStream(size_t idx, size_t eltCapacity, Vector<T>* data)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(encoder_);
        auto ptr = reserveStream(idx, eltCapacity, sizeof(T));
        ZL_ERR_IF_NULL(ptr, allocation);
        data->wrap((T*)ptr, eltCapacity);
        return ZL_returnSuccess();
    }

    virtual ZL_Report commitStream(size_t idx, size_t eltCount)
    {
        return ZL_Output_commit(streams_[idx], eltCount);
    }

    virtual void writeHeader(poly::string_view header)
    {
        ZL_Encoder_sendCodecHeader(encoder_, header.data(), header.size());
    }

   public:
    Transform(ZL_Encoder* encoder) : encoder_(encoder) {}

    virtual ~Transform() = default;

    static void encode1v4(
            Vector<uint8_t>& lits,
            Vector<length_t>& litLens,
            Vector<offset_t>& offsets,
            Vector<length_t>& matchLens,
            uint8_t const* const istart,
            uint8_t const* const iend)
    {
        auto mem = std::make_unique_for_overwrite<uint8_t[]>(
                ZS_FastTagTable_tableSize(kTableLog));
        ZS_FastTagTable table{};
        ZS_FastTagTable_init(&table, mem.get(), kTableLog, kMinMatch);
        uint8_t const* anchor = istart;
        uint8_t const* ip     = istart + 1;
        uint8_t const* const ilimit =
                iend - std::max<size_t>(kMinMatch + 1, 16 + 16);

        lits.resize(iend - istart + 16);
        litLens.resize((iend - istart + kMinMatch) / kMinMatch);
        offsets.resize((iend - istart + kMinMatch) / kMinMatch);
        matchLens.resize((iend - istart + kMinMatch) / kMinMatch);

        uint8_t* litsPtr = lits.data();
        size_t seq       = 0;

        while (ip < ilimit) {
            auto entry = ZS_FastTagTable_getAndUpdateT(
                    &table, ip, ip - istart, kMinMatch);
            auto ml             = boundML(matchLength(entry, ip));
            auto* match         = istart + entry.pos;
            const auto distance = ip - match;
            if (ml >= kMinMatch && valueFits(distance, sizeof(offset_t))
                && entry.pos != 0) {
                if (ZL_UNLIKELY(ml >= ZS_FastTagTable_kMaxMatchLen)) {
                    ZL_ASSERT_EQ(
                            memcmp(match, ip, ZS_FastTagTable_kMaxMatchLen), 0);
                    ml = boundML(
                            ZS_FastTagTable_kMaxMatchLen
                            + matchLength(
                                    match + ZS_FastTagTable_kMaxMatchLen,
                                    ip + ZS_FastTagTable_kMaxMatchLen,
                                    iend));
                } else {
                    ZL_ASSERT_EQ(memcmp(match, ip, ml), 0, "ml = %u", ml);
                }
                while (match > istart && ip > anchor && match[-1] == ip[-1]) {
                    --match;
                    --ip;
                    ++ml;
                }
                ZL_ASSERT_EQ(memcmp(match, ip, ml), 0);
                size_t ll = ip - anchor;
                offset_t const offset =
                        kIsIndex ? (match - istart) : (ip - match);

                memcpy(litsPtr, anchor, 16);
                if (ZL_UNLIKELY(ll > 16)) {
                    ZS_wildcopy(litsPtr, anchor, ll, ZS_wo_no_overlap);
                }
                litsPtr += ll;

                if (ZL_LIKELY(valueFits(ll, sizeof(length_t)))) {
                    litLens[seq] = ll;
                } else {
                    while (!valueFits(ll, sizeof(length_t))) {
                        litLens[seq]   = std::numeric_limits<length_t>::max();
                        matchLens[seq] = 0;
                        offsets[seq]   = kNoOpOffset;
                        ++seq;
                        ll -= std::numeric_limits<length_t>::max();
                    }
                    litLens[seq] = ll;
                }
                matchLens[seq] = ml;
                offsets[seq]   = offset;
                ++seq;
                ZS_FastTagTable_putT(
                        &table, ip + 2, ip + 2 - istart, kMinMatch);
                ip += ml;
                if (ip < ilimit) {
                    ZS_FastTagTable_putT(
                            &table, ip - 2, ip - 2 - istart, kMinMatch);
                }
                anchor = ip;
            } else {
                ip += 1;
            }
        }
        memcpy(litsPtr, anchor, (iend - anchor));
        litsPtr += (iend - anchor);

        lits.resize(litsPtr - lits.data());
        litLens.resize(seq);
        matchLens.resize(seq);
        offsets.resize(seq);
    }

    static void encode1v3(
            Vector<uint8_t>& lits,
            Vector<length_t>& litLens,
            Vector<offset_t>& offsets,
            Vector<length_t>& matchLens,
            uint8_t const* const istart,
            uint8_t const* const iend)
    {
        auto mem = std::make_unique_for_overwrite<uint8_t[]>(
                ZS_FastTable_tableSize(kTableLog) * 4);

        constexpr size_t kNumStates = 3;

        lits.resize(iend - istart + 16 + kNumStates);
        litLens.resize((iend - istart + kMinMatch) / kMinMatch + kNumStates);
        offsets.resize((iend - istart + kMinMatch) / kMinMatch + kNumStates);
        matchLens.resize((iend - istart + kMinMatch) / kMinMatch + kNumStates);

        struct State {
            State() {}
            State(size_t idx,
                  uint8_t* mem,
                  Vector<uint8_t>& lits,
                  Vector<length_t>& litLens,
                  Vector<offset_t>& offsets,
                  Vector<length_t>& matchLens,
                  const uint8_t* istart_,
                  const uint8_t* iend_)
            {
                const size_t segmentSize =
                        (iend_ - istart_ + kNumStates - 1) / kNumStates;
                const size_t segmentSeqs =
                        (segmentSize + kMinMatch - 1) / kMinMatch;
                const size_t tableSize = ZS_FastTable_tableSize(kTableLog);
                ZS_FastTable_init(
                        &table, mem + tableSize * idx, kTableLog, kMinMatch);
                istart = ip = anchor = istart_ + idx * segmentSize;
                if (idx + 1 == kNumStates) {
                    iend   = iend_;
                    ilimit = iend_ - 20;
                } else {
                    iend   = ip + segmentSize;
                    ilimit = iend - 20;
                }
                litsBegin = litsPtr = lits.data() + idx * segmentSize;
                llPtr               = litLens.data() + idx * segmentSeqs;
                mlPtr               = matchLens.data() + idx * segmentSeqs;
                offPtr              = offsets.data() + idx * segmentSeqs;

                *litsPtr++ = *ip++;
            }

            ZS_FastTable table{};
            uint8_t* litsBegin;
            uint8_t* litsPtr;
            length_t* llPtr;
            length_t* mlPtr;
            offset_t* offPtr;

            const uint8_t* anchor;
            const uint8_t* istart;
            const uint8_t* ip;
            const uint8_t* ilimit;
            const uint8_t* iend;
            size_t lastLiterals;

            size_t seq{ 0 };

            bool finished() const
            {
                return ip >= ilimit;
            }

            void step()
            {
                ZL_ASSERT(!finished());
                uint8_t const* match =
                        istart
                        + ZS_FastTable_getAndUpdateT(
                                &table, ip, ip - istart, kMinMatch);
                uint32_t distance  = ip - match;
                match              = distance >= 65536 ? ip - 1 : match;
                distance           = distance >= 65536 ? 1 : distance;
                length_t ml        = boundML(matchLength(match, ip, iend));
                ml                 = ml == 0 ? 1 : ml;
                const bool isMatch = (ml >= kMinMatch);

                // ZS_FastTable_conditionalPutT(
                //         &table, ip + 2, ip + 2 - istart, kMinMatch, isMatch);
                size_t ll = ip - anchor;
                if (ZL_LIKELY(valueFits(ll, sizeof(length_t)))) {
                    llPtr[seq] = ll;
                } else {
                    while (!valueFits(ll, sizeof(length_t))) {
                        llPtr[seq]  = std::numeric_limits<length_t>::max();
                        mlPtr[seq]  = 0;
                        offPtr[seq] = kNoOpOffset;
                        ++seq;
                        ll -= std::numeric_limits<length_t>::max();
                    }
                    llPtr[seq] = ll;
                }
                offPtr[seq] = distance;
                mlPtr[seq]  = ml;

                seq += isMatch ? 1 : 0;

                static_assert(kMinMatch <= 8);
                memcpy(litsPtr, ip, 8);
                litsPtr += isMatch ? 0 : ml;

                ip += ml;
                anchor = isMatch ? ip : anchor;
            }

            void writeLastLiterals()
            {
                ZL_ASSERT_LE(ilimit, iend);
                ZL_ASSERT_LE(ip, iend);
                memcpy(litsPtr, ip, (iend - ip));
                litsPtr += (iend - ip);
                lastLiterals = iend - anchor;
            }

            void appendTo(State& out) const
            {
                const size_t numLits = litsPtr - litsBegin;
                memmove(out.litsPtr, litsBegin, numLits);
                out.litsPtr += numLits;

                memmove(out.llPtr + out.seq, llPtr, seq * sizeof(out.llPtr[0]));
                memmove(out.mlPtr + out.seq, mlPtr, seq * sizeof(out.mlPtr[0]));
                memmove(out.offPtr + out.seq,
                        offPtr,
                        seq * sizeof(out.offPtr[0]));

                if (seq > 0) {
                    out.llPtr[out.seq] += out.lastLiterals;
                    out.lastLiterals = lastLiterals;
                } else {
                    out.lastLiterals += lastLiterals;
                }
                out.seq += seq;
            }
            void validate(const uint8_t* istart_, const uint8_t* iend_)
            {
                const size_t segmentSize =
                        (iend_ - istart_ + kNumStates - 1) / kNumStates;
                const size_t segmentSeqs =
                        (segmentSize + kMinMatch - 1) / kMinMatch;
                ZL_ASSERT_LE(litsPtr, litsBegin + segmentSize);
                ZL_ASSERT_LE(seq, segmentSeqs);
            }
        };

        std::array<State, kNumStates> states;
        for (size_t i = 0; i < states.size(); ++i) {
            states[i] =
                    State(i,
                          mem.get(),
                          lits,
                          litLens,
                          offsets,
                          matchLens,
                          istart,
                          iend);
        }

        for (;;) {
            bool anyRan = false;
#pragma clang loop unroll(full)
            for (auto& state : states) {
                if (!state.finished()) {
                    state.step();
                    anyRan = true;
                }
            }
            if (!anyRan) {
                break;
            }
        }

        for (auto& state : states) {
            state.writeLastLiterals();
            state.validate(istart, iend);
        }

        for (size_t idx = 1; idx < states.size(); ++idx) {
            states[idx].appendTo(states[0]);
        }

        lits.resize(states[0].litsPtr - lits.data());
        litLens.resize(states[0].seq);
        offsets.resize(states[0].seq);
        matchLens.resize(states[0].seq);
    }

    static void encode1v2(
            Vector<uint8_t>& lits,
            Vector<length_t>& litLens,
            Vector<offset_t>& offsets,
            Vector<length_t>& matchLens,
            uint8_t const* const istart,
            uint8_t const* const iend)
    {
        auto mem = std::make_unique_for_overwrite<uint8_t[]>(
                ZS_FastTable_tableSize(kTableLog));
        ZS_FastTable table{};
        ZS_FastTable_init(&table, mem.get(), kTableLog, kMinMatch);
        uint8_t const* anchor = istart;
        uint8_t const* ip     = istart + 1;
        uint8_t const* const ilimit =
                iend - std::max<size_t>(kMinMatch + 1, 20);

        lits.resize(iend - istart + 16);
        litLens.resize((iend - istart + kMinMatch) / kMinMatch);
        offsets.resize((iend - istart + kMinMatch) / kMinMatch);
        matchLens.resize((iend - istart + kMinMatch) / kMinMatch);

        uint8_t* litsPtr = lits.data();
        uint32_t* llPtr  = (uint32_t*)litLens.data();
        offset_t* offPtr = (offset_t*)offsets.data();
        uint32_t* mlPtr  = (uint32_t*)matchLens.data();

        *litsPtr++ = *anchor;

        size_t seq = 0;

        // The branch-free algorithm is slower...
        // But, it is running at 1.3 instructions / cycle
        // That would allow us to run 2-3 more copies of the same loop in
        // parallel. Call it 3, because we'd have a larger resident memory
        // footprint. We could divide the input in 4, and run 4 copies of the
        // loop at a time.
        while (ip < ilimit) {
            uint8_t const* match = istart
                    + ZS_FastTable_getAndUpdateT(
                                           &table, ip, ip - istart, kMinMatch);
            uint32_t distance  = ip - match;
            match              = distance >= 65536 ? ip - 1 : match;
            distance           = distance >= 65536 ? 1 : distance;
            length_t ml        = boundML(matchLength(match, ip, iend));
            ml                 = ml == 0 ? 1 : ml;
            const bool isMatch = (ml >= kMinMatch);

            size_t ll = ip - anchor;
            if (ZL_LIKELY(valueFits(ll, sizeof(length_t)))) {
                llPtr[seq] = ll;
            } else {
                while (!valueFits(ll, sizeof(length_t))) {
                    llPtr[seq]  = std::numeric_limits<length_t>::max();
                    mlPtr[seq]  = 0;
                    offPtr[seq] = kNoOpOffset;
                    ++seq;
                    ll -= std::numeric_limits<length_t>::max();
                }
                litLens[seq] = ll;
            }
            offPtr[seq] = distance;
            mlPtr[seq]  = ml;

            seq += isMatch ? 1 : 0;

            static_assert(kMinMatch <= 8);
            memcpy(litsPtr, ip, 8);
            litsPtr += isMatch ? 0 : ml;

            ip += ml;
            anchor = isMatch ? ip : anchor;
        }
        memcpy(litsPtr, ip, (iend - ip));
        litsPtr += (iend - ip);

        lits.resize(litsPtr - lits.data());
        litLens.resize(seq);
        offsets.resize(seq);
        matchLens.resize(seq);
    }

    static __attribute__((noinline)) void encode1Trailing(
            Vector<uint8_t>& lits,
            Vector<length_t>& litLens,
            Vector<offset_t>& offsets,
            Vector<length_t>& matchLens,
            uint8_t const* const istart,
            uint8_t const* const iend)
    {
        constexpr size_t kTrail = 16;

        auto mem = std::make_unique_for_overwrite<uint8_t[]>(
                ZS_FastTable_tableSize(kTableLog));
        ZS_FastTable table{};
        ZS_FastTable_init(&table, mem.get(), kTableLog, kMinMatch);
        uint8_t const* anchor = istart;
        uint8_t const* ip     = istart + 1;
        uint8_t const* const ilimit =
                iend - std::max<size_t>(kMinMatch + 1, 20);

        lits.resize(iend - istart + 16);
        litLens.resize((iend - istart + kMinMatch) / kMinMatch);
        offsets.resize((iend - istart + kMinMatch) / kMinMatch);
        matchLens.resize((iend - istart + kMinMatch) / kMinMatch);

        auto* litsPtr = lits.data();
        size_t seq    = 0;

        std::queue<uint8_t const*> insertQueue;
        auto insert =
                [&table, &istart, &insertQueue, kTrail](uint8_t const* ptr) {
                    assert(kTrail >= 1);
                    insertQueue.push(ptr);
                    while (insertQueue.front() + kTrail <= ptr) {
                        auto p = insertQueue.front();
                        ZS_FastTable_putT(&table, p, p - istart, kMinMatch);
                        insertQueue.pop();
                    }
                    assert(!insertQueue.empty());
                };

        auto getAndUpdate =
                [&insert, &table, &istart, kTrail](uint8_t const* ptr) {
                    insert(ptr);
                    return istart + ZS_FastTable_getT(&table, ptr, kMinMatch);
                };

        while (ip < ilimit) {
            uint8_t const* match    = getAndUpdate(ip);
            uint32_t const distance = ip - match;
            if (ZL_read32(match) == ZL_read32(ip)
                && valueFits(distance, sizeof(offset_t))) {
                uint32_t ml = 4 + matchLength(match + 4, ip + 4, iend);
                while (match > istart && ip > anchor && match[-1] == ip[-1]) {
                    --match;
                    --ip;
                    ++ml;
                }
                ml        = boundML(ml);
                size_t ll = ip - anchor;
                offset_t const offset =
                        kIsIndex ? (match - istart) : (ip - match);

                memcpy(litsPtr, anchor, 16);
                if (ZL_UNLIKELY(ll > 16)) {
                    ZS_wildcopy(litsPtr, anchor, ll, ZS_wo_no_overlap);
                }
                litsPtr += ll;

                if (ZL_LIKELY(valueFits(ll, sizeof(length_t)))) {
                    litLens[seq] = ll;
                } else {
                    while (!valueFits(ll, sizeof(length_t))) {
                        litLens[seq]   = std::numeric_limits<length_t>::max();
                        matchLens[seq] = 0;
                        offsets[seq]   = kNoOpOffset;
                        ++seq;
                        ll -= std::numeric_limits<length_t>::max();
                    }
                    litLens[seq] = ll;
                }
                matchLens[seq] = ml;
                offsets[seq]   = offset;
                ++seq;

                insert(ip + 2);
                ip += ml;
                if (ip < ilimit) {
                    insert(ip - 2);
                }
                anchor = ip;
            } else {
                ip += 1;
            }
        }
        memcpy(litsPtr, anchor, (iend - anchor));
        litsPtr += (iend - anchor);

        lits.resize(litsPtr - lits.data());
        litLens.resize(seq);
        matchLens.resize(seq);
        offsets.resize(seq);
    }

    static __attribute__((noinline)) void encode1Quartet(
            Vector<uint8_t>& lits,
            Vector<length_t>& litLens,
            Vector<offset_t>& offsets,
            Vector<length_t>& matchLens,
            uint8_t const* const istart,
            uint8_t const* const iend)
    {
        constexpr size_t kTrail = kQuartetLength;

        auto mem = std::make_unique_for_overwrite<uint8_t[]>(
                ZS_FastTable_tableSize(kTableLog));
        ZS_FastTable table{};
        ZS_FastTable_init(&table, mem.get(), kTableLog, kMinMatch);
        uint8_t const* anchor = istart;
        uint8_t const* ip     = istart + kTrail;
        uint8_t const* const ilimit =
                iend - std::max<size_t>(kMinMatch + 1, 20);

        lits.resize(iend - istart + 16);
        litLens.resize((iend - istart + kMinMatch) / kMinMatch);
        offsets.resize((iend - istart + kMinMatch) / kMinMatch);
        matchLens.resize((iend - istart + kMinMatch) / kMinMatch);

        auto* litsPtr = lits.data();
        size_t seq    = 0;

        // Quartets must all be below the checkpoint
        const uint8_t* checkpoint = anchor;

        std::queue<uint8_t const*> insertQueue;
        auto insert =
                [&table, &istart, &insertQueue, &checkpoint, &seq, kTrail](
                        uint8_t const* ptr) {
                    assert(kTrail >= 1);
                    insertQueue.push(ptr);
                    while (insertQueue.front() + kTrail <= ptr
                           && (seq % kQuartetNum == 0
                               || insertQueue.front() + kTrail <= checkpoint)) {
                        auto p = insertQueue.front();
                        ZS_FastTable_putT(&table, p, p - istart, kMinMatch);
                        insertQueue.pop();
                    }
                    assert(!insertQueue.empty());
                };

        auto getAndUpdate =
                [&insert, &table, &istart, kTrail](uint8_t const* ptr) {
                    insert(ptr);
                    return istart + ZS_FastTable_getT(&table, ptr, kMinMatch);
                };

        while (ip < ilimit) {
            uint8_t const* match = getAndUpdate(ip);
            if (seq % kQuartetNum == 0) {
                assert(match + kTrail <= ip);
            } else {
                assert(match + kTrail <= checkpoint);
            }
            uint32_t const distance = ip - match;
            if (ZL_read32(match) == ZL_read32(ip)
                && valueFits(distance, sizeof(offset_t))) {
                uint32_t ml = 4 + matchLength(match + 4, ip + 4, iend);
                while (match > istart && ip > anchor && match[-1] == ip[-1]
                       && seq % kQuartetNum != 0) {
                    --match;
                    --ip;
                    ++ml;
                }
                ml        = boundML(ml);
                size_t ll = ip - anchor;
                offset_t const offset =
                        kIsIndex ? (match - istart) : (ip - match);

                memcpy(litsPtr, anchor, 16);
                if (ZL_UNLIKELY(ll > 16)) {
                    ZS_wildcopy(litsPtr, anchor, ll, ZS_wo_no_overlap);
                }
                litsPtr += ll;

                if (ZL_LIKELY(valueFits(ll, sizeof(length_t)))) {
                    litLens[seq] = ll;
                } else {
                    while (!valueFits(ll, sizeof(length_t))) {
                        litLens[seq]   = std::numeric_limits<length_t>::max();
                        matchLens[seq] = 0;
                        offsets[seq]   = kNoOpOffset;
                        ll -= std::numeric_limits<length_t>::max();
                        if (seq % kQuartetNum == 0) {
                            checkpoint = ip - ll;
                        }
                        ++seq;
                    }
                    litLens[seq] = ll;
                }
                matchLens[seq] = ml;
                offsets[seq]   = offset;
                if (seq % kQuartetNum == 0) {
                    checkpoint = ip;
                }
                ++seq;

                insert(ip + 2);
                ip += ml;
                if (ip < ilimit) {
                    insert(ip - 2);
                }
                anchor = ip;
            } else {
                ip += 1;
            }
        }
        memcpy(litsPtr, anchor, (iend - anchor));
        litsPtr += (iend - anchor);

        lits.resize(litsPtr - lits.data());
        litLens.resize(seq);
        matchLens.resize(seq);
        offsets.resize(seq);
    }

    static __attribute__((noinline)) void encode1(
            Vector<uint8_t>& lits,
            Vector<length_t>& litLens,
            Vector<offset_t>& offsets,
            Vector<length_t>& matchLens,
            uint8_t const* const istart,
            uint8_t const* const iend)
    {
        if (0) {
            return encode1v4(lits, litLens, offsets, matchLens, istart, iend);
        } else if (0) {
            return encode1v3(lits, litLens, offsets, matchLens, istart, iend);
        } else if (0) {
            return encode1v2(lits, litLens, offsets, matchLens, istart, iend);
        } else if (0) {
            return encode1Trailing(
                    lits, litLens, offsets, matchLens, istart, iend);
        }
        auto mem = std::make_unique_for_overwrite<uint8_t[]>(
                ZS_FastTable_tableSize(kTableLog));
        ZS_FastTable table{};
        ZS_FastTable_init(&table, mem.get(), kTableLog, kMinMatch);
        uint8_t const* anchor = istart;
        uint8_t const* ip     = istart + 1;
        uint8_t const* const ilimit =
                iend - std::max<size_t>(kMinMatch + 1, 20);

        lits.resize(iend - istart + 16);
        litLens.resize((iend - istart + kMinMatch) / kMinMatch);
        offsets.resize((iend - istart + kMinMatch) / kMinMatch);
        matchLens.resize((iend - istart + kMinMatch) / kMinMatch);

        auto* litsPtr = lits.data();
        size_t seq    = 0;

        while (ip < ilimit) {
            uint8_t const* match = istart
                    + ZS_FastTable_getAndUpdateT(
                                           &table, ip, ip - istart, kMinMatch);
            uint32_t const distance = ip - match;
            if (ZL_read32(match) == ZL_read32(ip)
                && valueFits(distance, sizeof(offset_t))) {
                uint32_t ml = 4 + matchLength(match + 4, ip + 4, iend);
                while (match > istart && ip > anchor && match[-1] == ip[-1]) {
                    --match;
                    --ip;
                    ++ml;
                }
                ml        = boundML(ml);
                size_t ll = ip - anchor;
                offset_t const offset =
                        kIsIndex ? (match - istart) : (ip - match);

                memcpy(litsPtr, anchor, 16);
                if (ZL_UNLIKELY(ll > 16)) {
                    ZS_wildcopy(litsPtr, anchor, ll, ZS_wo_no_overlap);
                }
                litsPtr += ll;

                if (ZL_LIKELY(valueFits(ll, sizeof(length_t)))) {
                    litLens[seq] = ll;
                } else {
                    while (!valueFits(ll, sizeof(length_t))) {
                        litLens[seq]   = std::numeric_limits<length_t>::max();
                        matchLens[seq] = 0;
                        offsets[seq]   = kNoOpOffset;
                        ++seq;
                        ll -= std::numeric_limits<length_t>::max();
                    }
                    litLens[seq] = ll;
                }
                matchLens[seq] = ml;
                offsets[seq]   = offset;
                ++seq;

                ZS_FastTable_putT(&table, ip + 2, ip + 2 - istart, kMinMatch);
                ip += ml;
                if (ip < ilimit) {
                    ZS_FastTable_putT(
                            &table, ip - 2, ip - 2 - istart, kMinMatch);
                }
                anchor = ip;
            } else {
                ip += 1;
            }
        }
        memcpy(litsPtr, anchor, (iend - anchor));
        litsPtr += (iend - anchor);

        lits.resize(litsPtr - lits.data());
        litLens.resize(seq);
        matchLens.resize(seq);
        offsets.resize(seq);
    }

    static void encode2(
            Vector<uint8_t>& lits,
            Vector<length_t>& litLens,
            Vector<offset_t>& offsets,
            Vector<length_t>& matchLens,
            uint8_t const* const istart,
            uint8_t const* const iend)
    {
        auto smallMem = std::make_unique_for_overwrite<uint8_t[]>(
                ZS_FastTable_tableSize(16));
        auto largeMem = std::make_unique_for_overwrite<uint8_t[]>(
                ZS_FastTable_tableSize(17));
        ZS_FastTable smallT{};
        ZS_FastTable_init(&smallT, smallMem.get(), 16, kSmallMatch);
        ZS_FastTable largeT{};
        ZS_FastTable_init(&largeT, largeMem.get(), 17, kLargeMatch);
        uint8_t const* anchor       = istart;
        uint8_t const* ip           = istart + 1;
        uint8_t const* const ilimit = iend - kLargeMatch;

        auto emitMatch = [&](uint8_t const* match, uint8_t const* ptr) {
            uint32_t ml = matchLength(match, ptr, iend);
            if (ml < 4) {
                ZL_REQUIRE(false);
                ++ip;
                return;
            }
            ZL_ASSERT_GE(
                    ml,
                    4,
                    "%u to %u",
                    unsigned(match - istart),
                    unsigned(ptr - istart));
            while (match > istart && ptr > anchor && match[-1] == ptr[-1]) {
                --match;
                --ptr;
                ++ml;
            }
            ml                    = boundML(ml);
            size_t ll             = ptr - anchor;
            offset_t const offset = kIsIndex ? (match - istart) : (ptr - match);

            lits.insert(lits.end(), anchor, ptr);
            if (ZL_LIKELY(valueFits(ll, sizeof(length_t)))) {
                litLens.push_back(ll);
            } else {
                while (!valueFits(ll, sizeof(length_t))) {
                    litLens.push_back(std::numeric_limits<length_t>::max());
                    matchLens.push_back(0);
                    offsets.push_back(kNoOpOffset);
                    ll -= std::numeric_limits<length_t>::max();
                }
                litLens.push_back(ll);
            }
            litLens.push_back(ll);
            offsets.push_back(offset);
            matchLens.push_back(ml);
            //   ZS_FastTable_putT(&largeT, ip + 1, ip + 1 - istart,
            //   kLargeMatch);

            ZS_FastTable_putT(&smallT, ip + 2, ip + 2 - istart, kSmallMatch);
            ZS_FastTable_putT(&largeT, ip + 2, ip + 2 - istart, kLargeMatch);

            ip     = ptr + ml;
            anchor = ip;

            if (ip <= ilimit) {
                ZS_FastTable_putT(
                        &smallT, ip - 1, ip - 1 - istart, kSmallMatch);
                ZS_FastTable_putT(
                        &largeT, ip - 2, ip - 2 - istart, kLargeMatch);
            }
        };
        while (ip < ilimit) {
            uint8_t const* matchL =
                    istart
                    + ZS_FastTable_getAndUpdateT(
                            &largeT, ip, ip - istart, kLargeMatch);
            uint8_t const* matchS =
                    istart
                    + ZS_FastTable_getAndUpdateT(
                            &smallT, ip, ip - istart, kSmallMatch);
            if (ZL_read64(matchL) == ZL_read64(ip)
                && valueFits(ip - matchL, sizeof(offset_t))) {
                ZS_FastTable_putT(
                        &largeT, ip + 1, ip + 1 - istart, kLargeMatch);
                emitMatch(matchL, ip);
            } else if (
                    ZL_read32(matchS) == ZL_read32(ip)
                    && valueFits(ip - matchS, sizeof(offset_t))) {
                uint8_t const* matchL1 =
                        istart
                        + ZS_FastTable_getAndUpdateT(
                                &largeT, ip + 1, ip + 1 - istart, kLargeMatch);
                if (ZL_read64(matchL1) == ZL_read64(ip + 1)
                    && valueFits(ip + 1 - matchL1, sizeof(offset_t))) {
                    emitMatch(matchL1, ip + 1);
                } else {
                    emitMatch(matchS, ip);
                }
            } else {
                ip += 1;
            }
        }

        lits.insert(lits.end(), anchor, iend);
    }

    template <size_t kLocalLLBits, size_t kLocalMLBits>
    __attribute__((noinline)) ZL_Report
    tokenize(const Vector<length_t>& litLens, const Vector<length_t>& matchLens)
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(encoder_);

        Vector<uint8_t> tokens;
        Vector<length_t> extraLens;
        ZL_ERR_IF_ERR(reserveStream(1, litLens.size(), &tokens));
        // Extra padding for SIMD stores (8 elements max overshoot per store)
        ZL_ERR_IF_ERR(reserveStream(3, 2 * litLens.size() + 8, &extraLens));

        const uint32_t llMask = (1u << kLocalLLBits) - 1;
        const uint32_t mlMask = (1u << kLocalMLBits) - 1;

        size_t extraLenIdx = 0;
        size_t i           = 0;

#if ZL_ARCH_X86_64
        if constexpr (sizeof(length_t) == 2) {
            // Vectorized loop: process 8 uint16_t values at a time
            // using 128-bit SSE registers.
            const size_t kVecSize = 8; // 8 x uint16_t = 128 bits
            const size_t vecEnd   = litLens.size() & ~(kVecSize - 1);

            const __m128i llMaskVec =
                    _mm_set1_epi16(static_cast<int16_t>(llMask));
            const __m128i mlMaskVec =
                    _mm_set1_epi16(static_cast<int16_t>(mlMask));

            for (; i < vecEnd; i += kVecSize) {
                // Load 8 litLens and 8 matchLens (each uint16_t)
                __m128i llVec = _mm_loadu_si128(
                        reinterpret_cast<const __m128i*>(&litLens[i]));
                __m128i mlVec = _mm_loadu_si128(
                        reinterpret_cast<const __m128i*>(&matchLens[i]));

                // Compute llToken = min(ll, llMask) and mlToken = min(ml,
                // mlMask)
                __m128i llTokenVec = _mm_min_epu16(llVec, llMaskVec);
                __m128i mlTokenVec = _mm_min_epu16(mlVec, mlMaskVec);

                // Shift mlToken left by kLocalLLBits
                __m128i mlShifted = _mm_slli_epi16(mlTokenVec, kLocalLLBits);

                // Combine: token = llToken | (mlToken << kLocalLLBits)
                __m128i tokenVec16 = _mm_or_si128(llTokenVec, mlShifted);

                // Pack 16-bit tokens to 8-bit (we know they fit in 8 bits)
                __m128i tokenVec8 = _mm_packus_epi16(tokenVec16, tokenVec16);

                // Store 8 bytes of tokens
                _mm_storel_epi64(
                        reinterpret_cast<__m128i*>(&tokens[i]), tokenVec8);

                // Compute extra values: extraLL = ll - llMask, extraML = ml -
                // mlMask
                __m128i extraLLVec = _mm_sub_epi16(llVec, llMaskVec);
                __m128i extraMLVec = _mm_sub_epi16(mlVec, mlMaskVec);

                // Interleave extra values: [ll0,ml0,ll1,ml1,ll2,ml2,ll3,ml3]
                __m128i loInterleaved =
                        _mm_unpacklo_epi16(extraLLVec, extraMLVec);
                // [ll4,ml4,ll5,ml5,ll6,ml6,ll7,ml7]
                __m128i hiInterleaved =
                        _mm_unpackhi_epi16(extraLLVec, extraMLVec);

                // Check for extraLens: compare tokens with masks
                // Use equality with the min result to handle unsigned values
                // correctly. If ll >= llMask, then min(ll, llMask) == llMask.
                // Note: _mm_cmpgt_epi16 is signed, which fails for uint16_t >=
                // 32768.
                __m128i llEqMask = _mm_cmpeq_epi16(llTokenVec, llMaskVec);
                __m128i mlEqMask = _mm_cmpeq_epi16(mlTokenVec, mlMaskVec);

                // Interleave the 16-bit comparison masks, then pack to 8-bit
                // This gives us the masks already in the correct interleaved
                // order [ll0,ml0,ll1,ml1,ll2,ml2,ll3,ml3] and
                // [ll4,ml4,ll5,ml5,ll6,ml6,ll7,ml7]
                __m128i loEqInterleaved =
                        _mm_unpacklo_epi16(llEqMask, mlEqMask);
                __m128i hiEqInterleaved =
                        _mm_unpackhi_epi16(llEqMask, mlEqMask);

                // Pack to 8-bit: _mm_packs_epi16 saturates signed 16-bit to
                // signed 8-bit 0x0000 -> 0x00, 0xFFFF (-1) -> 0xFF
                __m128i eqPacked =
                        _mm_packs_epi16(loEqInterleaved, hiEqInterleaved);

                // Get 16-bit mask: low 8 bits for loMask, high 8 bits for
                // hiMask
                uint32_t combinedMask =
                        static_cast<uint32_t>(_mm_movemask_epi8(eqPacked));
                uint8_t loMask = combinedMask & 0xFF;
                uint8_t hiMask = (combinedMask >> 8) & 0xFF;

                // Use LUT to get shuffle patterns for pshufb
                __m128i loShuf = _mm_loadu_si128(
                        reinterpret_cast<const __m128i*>(
                                kShuffleLUT[loMask].data()));
                __m128i hiShuf = _mm_loadu_si128(
                        reinterpret_cast<const __m128i*>(
                                kShuffleLUT[hiMask].data()));

                // Shuffle to pack valid elements to the front
                __m128i loPacked = _mm_shuffle_epi8(loInterleaved, loShuf);
                __m128i hiPacked = _mm_shuffle_epi8(hiInterleaved, hiShuf);

                // Count valid elements and store
                size_t loCount = __builtin_popcount(loMask);
                size_t hiCount = __builtin_popcount(hiMask);

                // Store packed valid elements (may overwrite beyond valid
                // count, but we have padding in extraLens)
                _mm_storeu_si128(
                        reinterpret_cast<__m128i*>(&extraLens[extraLenIdx]),
                        loPacked);
                extraLenIdx += loCount;
                _mm_storeu_si128(
                        reinterpret_cast<__m128i*>(&extraLens[extraLenIdx]),
                        hiPacked);
                extraLenIdx += hiCount;
            }
        }
#endif

        // Scalar tail loop for remaining elements
        for (; i < litLens.size(); ++i) {
            const uint32_t ll     = litLens[i];
            const uint32_t ml     = matchLens[i];
            uint8_t const llToken = ll < llMask ? ll : llMask;
            uint8_t const mlToken = ml < mlMask ? ml : mlMask;
            uint8_t const token   = llToken | (mlToken << kLocalLLBits);

            tokens[i] = token;
            if (llToken == llMask) {
                extraLens[extraLenIdx++] = ll - llMask;
            }
            if (mlToken == mlMask) {
                extraLens[extraLenIdx++] = ml - mlMask;
            }
        }

        extraLens.resize(extraLenIdx);

        ZL_ERR_IF_ERR(commitStream(1, tokens.size()));
        ZL_ERR_IF_ERR(commitStream(3, extraLens.size()));

        return ZL_returnSuccess();
    }

    static ZL_Report encode(ZL_Encoder* encoder, ZL_Input const* input) noexcept
    {
        Transform tr(encoder);
        auto level = ZL_Encoder_getCParam(encoder, ZL_CParam_compressionLevel);
        size_t llParam = ZL_Encoder_getLocalIntParam(encoder, 0).paramValue;
        return tr.encode(
                { (const char*)ZL_Input_ptr(input), ZL_Input_numElts(input) },
                level,
                llParam);
    }

    ZL_Report
    encode(poly::string_view input, int level, size_t llParam) noexcept
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(encoder_);
        if (kIsIndex && input.size() >= 65536) {
            ZL_ERR(GENERIC, "Input too large for index mode");
        }

        Vector<uint8_t> lits;
        Vector<length_t> litLens;
        Vector<length_t> matchLens;
        Vector<offset_t> offsets;

        const size_t numElts = input.size();
        ZL_ERR_IF_ERR(reserveStream(0, numElts + 4 * 16 + 16, &lits));
        ZL_ERR_IF_ERR(reserveStream(2, numElts / kMinMatch + 16, &offsets));

        uint8_t const* const istart = (uint8_t const*)input.data();
        uint8_t const* const iend   = istart + numElts;

        if (kQuartet) {
            encode1Quartet(lits, litLens, offsets, matchLens, istart, iend);
        } else if (level == 1) {
            encode1(lits, litLens, offsets, matchLens, istart, iend);
        } else {
            encode2(lits, litLens, offsets, matchLens, istart, iend);
        }

        if (llParam == 0) {
            std::array<uint32_t, 256> llStats{ 0 };
            std::array<uint32_t, 256> mlStats{ 0 };
            for (size_t i = 0; i < litLens.size(); ++i) {
                ++llStats[std::min<uint32_t>(litLens[i], 255)];
                ++mlStats[std::min<uint32_t>(matchLens[i], 255)];
            }
            size_t bestLLBits    = 0;
            size_t bestExtraLens = 2 * litLens.size() + 1;
            for (size_t llBits = 1; llBits < 8; ++llBits) {
                const size_t mlBits = 8 - llBits;
                const size_t llMask = (1u << llBits) - 1;
                const size_t mlMask = (1u << mlBits) - 1;

                size_t numExtraLL = 0;
                for (size_t i = llMask; i < llStats.size(); ++i) {
                    numExtraLL += llStats[i];
                }
                size_t numExtraML = 0;
                for (size_t i = mlMask; i < mlStats.size(); ++i) {
                    numExtraML += mlStats[i];
                }
                size_t numExtraLens = numExtraLL + numExtraML;

                // Need some way to take branch predictibility into account.
                // Weigh in favor of predictable branches when it is relatively
                // close.
                // const size_t predictableCutoff = litLens.size() / 64;
                // if (numExtraLL < predictableCutoff) {
                //     numExtraLens -= numExtraLens / 16;
                // }
                // if (numExtraML < predictableCutoff) {
                //     numExtraLens -= numExtraLens / 16;
                // }

                // fprintf(stderr,
                //         "%zu: %zu -> %zu = %zu + %zu (%zu)\n",
                //         litLens.size(),
                //         llBits,
                //         numExtraLens,
                //         numExtraLL,
                //         numExtraML,
                //         bestExtraLens);
                if (numExtraLens < bestExtraLens) {
                    bestExtraLens = numExtraLens;
                    bestLLBits    = llBits;
                }
            }
            llParam = bestLLBits;
        }

        size_t llBits = llParam;
        size_t mlBits = 8 - llBits;
        // fprintf(stderr, "Selected llbits = %zu\n", llBits);

        ZL_Report report;
        if (llBits == 1) {
            report = tokenize<1, 8 - 1>(litLens, matchLens);
        } else if (llBits == 2) {
            report = tokenize<2, 8 - 2>(litLens, matchLens);
        } else if (llBits == 3) {
            report = tokenize<3, 8 - 3>(litLens, matchLens);
        } else if (llBits == 4) {
            report = tokenize<4, 8 - 4>(litLens, matchLens);
        } else if (llBits == 5) {
            report = tokenize<5, 8 - 5>(litLens, matchLens);
        } else if (llBits == 6) {
            report = tokenize<6, 8 - 6>(litLens, matchLens);
        } else if (llBits == 7) {
            report = tokenize<7, 8 - 7>(litLens, matchLens);
        } else {
            ZL_ERR(GENERIC, "Bad LLBits %zu", llBits);
        }
        ZL_ERR_IF_ERR(report);

        uint8_t header[ZL_VARINT_LENGTH_64 + 1];
        header[0]               = llBits | (mlBits << 4);
        uint32_t const srcSize  = input.size();
        const size_t headerSize = 1 + ZL_varintEncode(srcSize, header + 1);
        writeHeader({ (const char*)header, headerSize });

        // ZL_ERR_IF_ERR(writeStream(eictx, 0, lits));
        // ZL_ERR_IF_ERR(writeStream(eictx, 2, offsets));
        ZL_ERR_IF_ERR(commitStream(0, lits.size()));
        ZL_ERR_IF_ERR(commitStream(2, offsets.size()));

        return ZL_returnSuccess();
    }

    template <size_t kLen>
    static void copy(void* dst, void const* src)
    {
#if defined(__AVX2__)
        if constexpr (kLen == 16) {
            auto v = _mm_lddqu_si128((const __m128i_u*)src);
            _mm_storeu_si128((__m128i_u*)dst, v);
            return;
        }
        if constexpr (kLen == 32) {
            auto v = _mm256_lddqu_si256((const __m256i_u*)src);
            _mm256_storeu_si256((__m256i_u*)dst, v);
            return;
        }
#endif

        char tmp[kLen];
        memcpy(tmp, src, kLen);
        memcpy(dst, tmp, kLen);
    }

   public:
    static size_t decodeImpl2(
            uint8_t const* __restrict lits,
            size_t numLits,
            uint8_t const* __restrict tokens,
            offset_t const* __restrict offsets,
            size_t numSeqs,
            length_t const* __restrict extraLens,
            size_t numExtraLens,
            uint8_t* __restrict out,
            size_t outCapacity)
    {
        ZL_REQUIRE(kIsIndex);
        static_assert(kLLBits + kMLBits == 8, "");
        uint8_t* const base           = out;
        auto litEnd                   = lits + numLits;
        uint8_t const* const outLimit = out + outCapacity
                - 4 * std::max(16, 1 << std::max(kLLBits, kMLBits));
        uint8_t const* const litLimit =
                lits + numLits - 4 * std::max(16, 1 << kLLBits);
        size_t i = 0;
        for (;;) {
            if (i + 4 > numSeqs) {
                break;
            }
            if (out > outLimit) {
                break;
            }
            if (lits > litLimit) {
                break;
            }
            std::array<std::array<uint8_t, 1 << kMLBits>, 4> matches;
#pragma clang loop unroll(full)
            for (size_t u = 0; u < 4; ++u) {
                memcpy(matches[u].data(), base + offsets[i + u], 1 << kMLBits);
            }

            // TODO: If I control this loop carefully with intrinsics, can it be
            // faster?
#pragma clang loop unroll(full)
            for (size_t u = 0; u < 4; ++u) {
                size_t ll = tokens[i] & kLLMask;
                size_t ml = (tokens[i] >> kLLBits) & kMLMask;
                memcpy(out, lits, 1 << kLLBits);
                if (ZL_UNLIKELY(ll == kLLMask)) {
                    memcpy(out + kLLMask, lits + kLLMask, *extraLens);
                    ll += *extraLens++;
                    if (ZL_UNLIKELY(
                                out + ll > outLimit || lits + ll > litLimit)) {
                        out += ll;
                        lits += ll;
                        if (ZL_UNLIKELY(ml == kMLMask)) {
                            ml += *extraLens++;
                        }
                        memcpy(out, base + offsets[i], ml);
                        out += ml;
                        ++i;
                        break;
                    }
                }
                out += ll;
                lits += ll;

                memcpy(out, matches[u].data(), 1 << kMLBits);
                if (ZL_UNLIKELY(ml == kMLMask)) {
                    memcpy(out + kMLMask,
                           base + offsets[i] + kMLMask,
                           *extraLens);
                    ml += *extraLens++;
                    if (ZL_UNLIKELY(out + ml > outLimit)) {
                        out += ml;
                        ++i;
                        break;
                    }
                }
                out += ml;
                ++i;
            }
        }
        while (i < numSeqs) {
            size_t ll = tokens[i] & kLLMask;
            size_t ml = (tokens[i] >> kLLBits) & kMLMask;
            if (ll == kLLMask) {
                ll += *extraLens++;
            }
            if (ml == kMLMask) {
                ml += *extraLens++;
            }
            memcpy(out, lits, ll);
            out += ll;
            lits += ll;
            uint8_t const* const match =
                    kIsIndex ? base + offsets[i] : out - offsets[i];
            memcpy(out, match, ml);
            out += ml;
            ++i;
        }
        memcpy(out, lits, (litEnd - lits));
        out += litEnd - lits;
        return out - base;
    }

#if ZL_ARCH_X86_64
    static void print16(char const* name, __m128i vec)
    {
        std::array<uint16_t, 8> array;
        _mm_storeu_si128((__m128i_u*)array.data(), vec);
        fprintf(stderr, "%s: ", name);
        for (auto const& val : array) {
            fprintf(stderr, "0x%x\t", val);
        }
        fprintf(stderr, "\n");
    }

    static void print8(char const* name, __m128i vec)
    {
        std::array<uint8_t, 16> array;
        _mm_storeu_si128((__m128i_u*)array.data(), vec);
        fprintf(stderr, "%s: ", name);
        for (auto const& val : array) {
            fprintf(stderr, "0x%x\t", val);
        }
        fprintf(stderr, "\n");
    }
#endif // ZL_ARCH_X86_64

    static size_t detokenize4Fallback(
            uint32_t tokens,
            length_t const* __restrict extraLens,
            length_t lens[8])
    {
        size_t needed = 0;
        for (size_t i = 0; i < 4; ++i) {
            const uint8_t token = tokens >> (i * 8);
            size_t ll           = token & kLLMask;
            size_t ml           = (token >> kLLBits) & kMLMask;
            if (ll == kLLMask) {
                ll += extraLens[needed++];
            }
            if (ml == kMLMask) {
                ml += extraLens[needed++];
            }
            lens[i * 2 + 0] = ll;
            lens[i * 2 + 1] = ml;
        }
        return needed;
    }

#if ZL_ARCH_X86_64
    static int detokenize4(
            uint32_t tokens,
            length_t const* __restrict extraLens,
            length_t lens[8])
    {
        // return detokenize4Fallback(tokens, extraLens, lens);
        // Load tokens in [0, 4), and (tokens >> kLLBits) in [8, 12)
        // TODO: Are there more efficient ways to load the tokens?
        const __m128i kTokensShuffleV = _mm_setr_epi8(
                0x00,
                0x08,
                0x01,
                0x09,
                0x02,
                0x0A,
                0x03,
                0x0B,
                0x80,
                0x80,
                0x80,
                0x80,
                0x80,
                0x80,
                0x80,
                0x80);
        const __m128i kTokensSpreadV = _mm_setr_epi8(
                0x00,
                0x80,
                0x01,
                0x80,
                0x02,
                0x80,
                0x03,
                0x80,
                0x04,
                0x80,
                0x05,
                0x80,
                0x06,
                0x80,
                0x07,
                0x80);
        // fprintf(stderr, "kLLBits = %d, kMLBits = %d\n", kLLBits, kMLBits);
        // Load 8 extra lengths
        __m128i extraLengthsV = _mm_loadu_si128((const __m128i_u*)extraLens);
        // fprintf(stderr,
        //         "tokens: [0x%x, 0x%x, 0x%x, 0x%x]\n",
        //         tokens & 0xFF,
        //         (tokens >> 8) & 0xFF,
        //         (tokens >> 16) & 0xFF,
        //         (tokens >> 24) & 0xFF);
        __m128i lengthsV = _mm_set_epi64x(tokens >> kLLBits, tokens);

        // Interleave the lls & mls into uint8_t
        // [ll0][ml0][ll1][ml1][ll2][ml2][ll3][ml3] 8x[0]
        lengthsV = _mm_shuffle_epi8(lengthsV, kTokensShuffleV);
        // print8("lengthsV", lengthsV);

        // Mask the lls & mls to only the bits needed
        const __m128i kMaskV =
                _mm_set1_epi16(uint16_t(kLLMask) | (uint16_t(kMLMask) << 8));
        lengthsV = _mm_and_si128(lengthsV, kMaskV);
        // print8("lengthsV & kMaskV", lengthsV);

        // Determine how many lengths we need
        const __m128i needsExtraV = _mm_cmpeq_epi8(lengthsV, kMaskV);
        const int needsExtra      = _mm_movemask_epi8(needsExtraV);
        assert(needsExtra < 256);
        // print8("needsExtraV", needsExtraV);

        // Unpack the lengthsV into uint16_t
        lengthsV = _mm_shuffle_epi8(lengthsV, kTokensSpreadV);
        // lengthsV = _mm_unpacklo_epi8(lengthsV, _mm_setzero_si128());
        // print16("lengthsV", lengthsV);

        // print16("extraLengthsV", extraLengthsV);

        // Shuffle the extra lengths into the correct positions
        const __m128i kExtraShuffleV = _mm_loadu_si128(
                (const __m128i_u*)&kDecodeShuffleLUT[needsExtra]);
        // print8("kExtraShuffleV", kExtraShuffleV);
        extraLengthsV = _mm_shuffle_epi8(extraLengthsV, kExtraShuffleV);
        // print16("extraLengthsV", extraLengthsV);

        // Add the extra lengths to the lengths
        lengthsV = _mm_add_epi16(lengthsV, extraLengthsV);
        // print16("lengthsV", lengthsV);

        // Store the result
        _mm_storeu_si128((__m128i_u*)lens, lengthsV);

#    ifndef NDEBUG
        {
            const size_t needed = _mm_popcnt_u32(needsExtra);
            uint16_t fbLens[8];
            assert(needed == detokenize4Fallback(tokens, extraLens, fbLens));
            assert(memcmp(lens, fbLens, sizeof(fbLens)) == 0);
        }
#    endif
        return needsExtra;
    }

    static size_t decodeImplV(
            uint8_t const* __restrict lits,
            size_t numLits,
            uint8_t const* __restrict tokens,
            offset_t const* __restrict offsets,
            size_t numSeqs,
            length_t const* __restrict extraLens,
            size_t numExtraLens,
            uint8_t* __restrict out,
            size_t outCapacity)
    {
        static_assert(kLLBits + kMLBits == 8, "");
        uint8_t* const base           = out;
        auto litEnd                   = lits + numLits;
        uint8_t const* const outLimit = out + outCapacity
                - std::max(ZS_WILDCOPY_OVERLENGTH,
                           1 << std::max(kLLBits, kMLBits))
                - 16;
        uint8_t const* const litLimit =
                lits + numLits - std::max(ZS_WILDCOPY_OVERLENGTH, 1 << kLLBits);

        size_t i = 0;
        if (numSeqs >= 4) {
            size_t limitSeq = numSeqs;
            for (size_t numExtra = 0; limitSeq > 0 && numExtra <= 8;) {
                --limitSeq;
                size_t ll = tokens[limitSeq] & kLLMask;
                size_t ml = (tokens[limitSeq] >> kLLBits) & kMLMask;
                numExtra += ll == kLLMask;
                numExtra += ml == kMLMask;
            }
            assert(numSeqs - limitSeq >= 4);
            // TODO: This is bad when there aren't sequences near the end that
            // have extra lengths Improve this to instead copy the extra lens to
            // a separate buffer when near the end, and read from that
            // alternative buffer instead
            for (; i < limitSeq; i += 4) {
                length_t lens[8];
                const int needsExtra =
                        detokenize4(ZL_read32(tokens + i), extraLens, lens);
                const size_t extrasNeeded = _mm_popcnt_u32(needsExtra);
                const size_t litSum = lens[0] + lens[2] + lens[4] + lens[6];
                const size_t outSum =
                        litSum + lens[1] + lens[3] + lens[5] + lens[7];
                if (lits + litSum > litLimit || out + outSum > outLimit) {
                    break;
                }
                extraLens += extrasNeeded;
                for (size_t u = 0; u < 4; ++u) {
                    size_t ll = lens[2 * u + 0];
                    size_t ml = lens[2 * u + 1];
                    assert(out + ll <= outLimit && lits + ll <= litLimit);

                    constexpr size_t kLLCopy = 1 << kLLBits;
                    copy<kLLCopy>(out, lits);
                    if (ZL_UNLIKELY(ll > kLLCopy)) {
                        ZS_wildcopy(
                                out + kLLCopy,
                                lits + kLLCopy,
                                ll - kLLCopy,
                                ZS_wo_no_overlap);
                    }
                    lits += ll;
                    out += ll;

                    const size_t off           = offsets[i + u];
                    uint8_t const* const match = out - off;
                    assert(out + ml <= outLimit);
                    const size_t kMLCopy = 1 << kMLBits;
                    if (ZL_LIKELY(off >= 16)) {
                        for (size_t i_2 = 0; i_2 < kMLCopy; i_2 += 16) {
                            memcpy(out + i_2, match + i_2, 16);
                        }
                    } else {
                        ZS_wildcopy(out, match, kMLCopy, ZS_wo_src_before_dst);
                    }
                    if (ml > kMLCopy) {
                        ZS_wildcopy(
                                out + kMLCopy,
                                match + kMLCopy,
                                ml - kMLCopy,
                                ZS_wo_src_before_dst);
                    }
                    out += ml;
                }
            }
        }
        for (; i < numSeqs; ++i) {
            size_t ll = tokens[i] & kLLMask;
            size_t ml = (tokens[i] >> kLLBits) & kMLMask;
            if (ll == kLLMask) {
                ll += *extraLens++;
            }
            if (ml == kMLMask) {
                ml += *extraLens++;
            }
            memcpy(out, lits, ll);
            out += ll;
            lits += ll;
            uint8_t const* const match = out - offsets[i];
            ZS_safecopy(out, match, ml, ZS_wo_src_before_dst);
            out += ml;
        }
        memcpy(out, lits, (litEnd - lits));
        out += litEnd - lits;
        return out - base;
    }
#endif // ZL_ARCH_X86_64

    // TODO: Lost speed here when fixing it
    static size_t decodeImpl(
            uint8_t const* __restrict lits,
            size_t numLits,
            uint8_t const* __restrict tokens,
            offset_t const* __restrict offsets,
            size_t numSeqs,
            length_t const* __restrict extraLens,
            size_t numExtraLens,
            uint8_t* __restrict out,
            size_t outCapacity)
    {
        static_assert(kLLBits + kMLBits == 8, "");
        uint8_t* const base           = out;
        auto litEnd                   = lits + numLits;
        uint8_t const* const outLimit = out + outCapacity
                - std::max(ZS_WILDCOPY_OVERLENGTH,
                           1 << std::max(kLLBits, kMLBits))
                - 16;
        uint8_t const* const litLimit =
                lits + numLits - std::max(ZS_WILDCOPY_OVERLENGTH, 1 << kLLBits);
        for (size_t i = 0; i < numSeqs; ++i) {
            size_t ll = tokens[i] & kLLMask;
            size_t ml = (tokens[i] >> kLLBits) & kMLMask;
            if (ZL_LIKELY(out < outLimit && lits < litLimit)) {
                copy<1 << kLLBits>(out, lits);
            } else {
                ZS_safecopy(out, lits, ll, ZS_wo_no_overlap);
            }
            if (ZL_UNLIKELY(ll == kLLMask)) {
                ll += *extraLens;
                if (ZL_LIKELY(out + ll <= outLimit && lits + ll <= litLimit)) {
                    ZS_wildcopy(
                            out + kLLMask,
                            lits + kLLMask,
                            *extraLens,
                            ZS_wo_no_overlap);
                } else {
                    ZS_safecopy(
                            out + kLLMask,
                            lits + kLLMask,
                            *extraLens,
                            ZS_wo_no_overlap);
                }
                ++extraLens;
            }
            lits += ll;
            out += ll;
            uint8_t const* const match =
                    kIsIndex ? base + offsets[i] : out - offsets[i];
            if (ZL_LIKELY(out < outLimit)) {
                // copy<1 << kMLBits>(out, match);
                if (ZL_LIKELY(offsets[i] >= ml)) {
                    // TODO: This is legal now
                    copy<1 << kMLBits>(out, match);
                    // for (size_t i_2 = 0; i_2 < (1 << kMLBits); i_2 += 16) {
                    //     memcpy(out + i_2, match + i_2, 16);
                    // }
                } else {
                    ZS_wildcopy(out, match, 1 << kMLBits, ZS_wo_src_before_dst);
                }
            } else {
                ZS_safecopy(out, match, ml, ZS_wo_src_before_dst);
            }
            if (ZL_UNLIKELY(ml == kMLMask)) {
                ml += *extraLens;
                if (ZL_LIKELY(out + ml <= outLimit)) {
                    ZS_wildcopy(
                            out + kMLMask,
                            match + kMLMask,
                            *extraLens,
                            ZS_wo_src_before_dst);
                } else {
                    ZS_safecopy(
                            out + kMLMask,
                            match + kMLMask,
                            *extraLens,
                            ZS_wo_src_before_dst);
                }
                ++extraLens;
            }
            out += ml;
        }
        memcpy(out, lits, (litEnd - lits));
        out += litEnd - lits;
        return out - base;
    }

    static size_t decodeImplQuartet(
            uint8_t const* __restrict lits,
            size_t numLits,
            uint8_t const* __restrict tokens,
            offset_t const* __restrict offsets,
            size_t numSeqs,
            length_t const* __restrict extraLens,
            size_t numExtraLens,
            uint8_t* __restrict out,
            size_t outCapacity)
    {
        static_assert(kLLBits + kMLBits == 8, "");
        uint8_t* const base           = out;
        auto litEnd                   = lits + numLits;
        uint8_t const* const outLimit = out + outCapacity
                - std::max(ZS_WILDCOPY_OVERLENGTH,
                           1 << std::max(kLLBits, kMLBits))
                - 16;
        uint8_t const* const litLimit =
                lits + numLits - std::max(ZS_WILDCOPY_OVERLENGTH, 1 << kLLBits);

        auto copyLiterals = [&out, outLimit, &lits, litLimit](size_t ll) {
            if (ZL_LIKELY(ll <= (1 << kLLBits))) {
                if (ZL_LIKELY(out < outLimit && lits < litLimit)) {
                    copy<1 << kLLBits>(out, lits);
                } else {
                    ZS_safecopy(out, lits, ll, ZS_wo_no_overlap);
                }
            } else {
                if (ZL_LIKELY(out + ll <= outLimit && lits + ll <= litLimit)) {
                    ZS_wildcopy(out, lits, ll, ZS_wo_no_overlap);
                } else {
                    ZS_safecopy(out, lits, ll, ZS_wo_no_overlap);
                }
            }
        };

        size_t i = 0;
        if (numSeqs >= kQuartetNum) {
            for (; i < numSeqs - kQuartetNum; i += kQuartetNum) {
                size_t lls[kQuartetNum];
                size_t mls[kQuartetNum];
                constexpr size_t kFirstCopyLength    = kQuartetLength;
                constexpr size_t kFirstCopyLengthU64 = kFirstCopyLength / 8;
                static_assert(kFirstCopyLength % sizeof(uint64_t) == 0);
                uint64_t data[kFirstCopyLengthU64 * kQuartetNum];

#pragma clang loop unroll(full)
                for (size_t u = 0, pos = 0; u < kQuartetNum; ++u) {
                    size_t ll = tokens[i + u] & kLLMask;
                    size_t ml = (tokens[i + u] >> kLLBits) & kMLMask;
                    if (ZL_UNLIKELY(ll == kLLMask)) {
                        ll += *extraLens++;
                    }
                    if (ZL_UNLIKELY(ml == kMLMask)) {
                        ml += *extraLens++;
                    }
                    if (u == 0) {
                        // Copy the first literals of the quartet so we can
                        // match into it
                        copyLiterals(ll);
                    }
                    lls[u] = ll;
                    mls[u] = ml;
                    pos += ll;
                    assert(offsets[i + u] >= (pos - lls[0]) + kFirstCopyLength);
                    memcpy(data + kFirstCopyLengthU64 * u,
                           out + pos - offsets[i + u],
                           kFirstCopyLength);
                    pos += ml;
                }
#pragma clang loop unroll(full)
                for (size_t u = 0; u < kQuartetNum; ++u) {
                    const size_t ll  = lls[u];
                    const size_t ml  = mls[u];
                    const size_t off = offsets[i + u];
                    if (u != 0) {
                        copyLiterals(ll);
                    }
                    lits += ll;
                    out += ll;

                    if (ZL_LIKELY(out < outLimit)) {
                        memcpy(out,
                               data + kFirstCopyLengthU64 * u,
                               kFirstCopyLength);
                    } else {
                        memcpy(out,
                               data + kFirstCopyLengthU64 * u,
                               ZL_MIN(kFirstCopyLength, ml));
                    }
                    if (ZL_UNLIKELY(ml > kFirstCopyLength)) {
                        uint8_t const* const match = out - off;
                        if (ZL_LIKELY(out + ml <= outLimit)) {
                            assert(out < outLimit);
                            ZS_wildcopy(
                                    out + kFirstCopyLength,
                                    match + kFirstCopyLength,
                                    ml - kFirstCopyLength,
                                    ZS_wo_src_before_dst);
                        } else {
                            ZS_safecopy(out, match, ml, ZS_wo_src_before_dst);
                        }
                    }
                    out += ml;
                }
            }
        }

        for (; i < numSeqs; ++i) {
            size_t ll = tokens[i] & kLLMask;
            size_t ml = (tokens[i] >> kLLBits) & kMLMask;
            if (ll == kLLMask) {
                ll += *extraLens++;
            }
            if (ml == kMLMask) {
                ml += *extraLens++;
            }
            memcpy(out, lits, ll);
            out += ll;
            lits += ll;
            uint8_t const* const match = out - offsets[i];
            memcpy(out, match, ml);
            out += ml;
        }
        memcpy(out, lits, (litEnd - lits));
        out += litEnd - lits;
        return out - base;
    }

    struct DecodeState {
        const uint8_t* lits;
        const uint8_t* litsLimit;
        const uint8_t* litsEnd;

        ptrdiff_t seq;

        const length_t* extraLens;
        const length_t* extraLensLimit;
        const length_t* extraLensEnd;

        ptrdiff_t out;
    };

    struct DecodeInfo {
        const uint8_t* tokens;
        const offset_t* offsets;
        ptrdiff_t numSeqs;
        ptrdiff_t outCapacity;
        uint8_t* outBase;
    };

    static constexpr std::array<ptrdiff_t, 8> kMaxLLs = { 0,  16, 16, 16,
                                                          32, 32, 64, 128 };
    static constexpr std::array<ptrdiff_t, 8> kMaxMLs = { 0,  32, 32, 32,
                                                          32, 32, 64, 128 };

    static constexpr ptrdiff_t kMaxLitLen   = kMaxLLs[kLLBits];
    static constexpr ptrdiff_t kMaxMatchLen = kMaxMLs[kMLBits];
    // std::max<ptrdiff_t>(32, 1u << kMLBits);
    static constexpr ptrdiff_t kIters = 4;

    static constexpr ptrdiff_t kExtraLensSlop = 8;
    static constexpr ptrdiff_t kLitsSlop      = kIters * kMaxLitLen;
    static constexpr ptrdiff_t kSeqSlop       = kIters;
    static constexpr ptrdiff_t kOutSlop = kIters * (kMaxLitLen + kMaxMatchLen);

    template <ptrdiff_t N, typename CopyN, typename BoundsCheck>
    __attribute__((always_inline)) static bool copyWithSunkenBoundsCheck(
            ptrdiff_t len,
            CopyN&& copyN,
            BoundsCheck&& boundsCheck)
    {
        copyN(0);
        if (ZL_UNLIKELY(len > N)) {
#if USE_LLVM_MCA
            return false;
#endif
            if (!boundsCheck()) {
                return false;
            }
            // Signed so the compiler can assume no overflow
            ptrdiff_t copied = N;
            do {
                copyN(copied);
                copied += N;
            } while (copied < len);
        }
        return true;
    }

    template <ptrdiff_t N, typename BoundsCheck>
    __attribute__((always_inline)) static bool copyWithSunkenBoundsCheck(
            uint8_t* out,
            const uint8_t* in,
            size_t len,
            BoundsCheck&& boundsCheck)
    {
        return copyWithSunkenBoundsCheck<N>(
                len,
                [out, in](size_t offset) {
                    copy<N>(out + offset, in + offset);
                },
                boundsCheck);
    }

    static constexpr std::array<std::array<uint8_t, 16>, 17> makePatternLUT(
            size_t startPos)
    {
        std::array<std::array<uint8_t, 16>, 17> lut;
        for (size_t offset = 0; offset < lut.size(); ++offset) {
            for (size_t pos = 0; pos < 16; ++pos) {
                if (offset == 0) {
                    lut[offset][pos] = 0x80;
                } else {
                    lut[offset][pos] = (startPos + pos) % offset;
                }
            }
        }
        return lut;
    }

    alignas(16) static constexpr std::
            array<std::array<uint8_t, 16>, 17> kPatternGeneration{
                makePatternLUT(0)
            };

    alignas(16) static constexpr std::
            array<std::array<uint8_t, 16>, 17> kPatternReshuffle{
                makePatternLUT(16)
            };

#if ZL_ARCH_X86_64
    static __m128i loadPattern(const uint8_t* src, offset_t offset)
    {
        assert(offset <= 16);
        auto pattern = _mm_load_si128(
                (const __m128i*)kPatternGeneration[offset].data());
        auto data = _mm_lddqu_si128((const __m128i_u*)src);
        return _mm_shuffle_epi8(data, pattern);
    }
#endif // ZL_ARCH_X86_64

#if ZL_ARCH_X86_64
    template <ptrdiff_t N, typename BoundsCheck>
    __attribute__((always_inline))
    __attribute__((flatten)) static bool copyOverlappingWithSunkenBoundsCheck(
            uint8_t* out,
            offset_t offset,
            size_t len,
            BoundsCheck&& boundsCheck)
    {
#    if USE_LLVM_MCA
        return false;
#    endif
        // TODO: < or <=
        // TODO: Always copy 64, or copy 32
        if (ZL_LIKELY(offset <= 16)) {
            switch (offset) {
                case 1: {
                    auto pattern = _mm256_set1_epi8(out[-1]);
                    return copyWithSunkenBoundsCheck<N>(
                            len,
                            [out, pattern](size_t offset) {
                                static_assert(N % 32 == 0);
                                for (size_t o = 0; o < N; o += 32) {
                                    _mm256_storeu_si256(
                                            (__m256i_u*)(out + offset + o),
                                            pattern);
                                }
                            },
                            boundsCheck);
                }
                case 2:
                case 4:
                case 8:
                case 16: {
                    auto pattern = loadPattern(out - offset, offset);
                    return copyWithSunkenBoundsCheck<N>(
                            len,
                            [out, pattern](size_t offset) {
                                static_assert(N % 16 == 0);
                                for (size_t o = 0; o < N; o += 16) {
                                    _mm_storeu_si128(
                                            (__m128i_u*)(out + offset + o),
                                            pattern);
                                }
                            },
                            boundsCheck);
                }
                default: {
                    auto pattern   = loadPattern(out - offset, offset);
                    auto reshuffle = _mm_load_si128(
                            (const __m128i*)kPatternReshuffle[offset].data());
                    return copyWithSunkenBoundsCheck<N>(
                            len,
                            [out, pattern, reshuffle](size_t offset) mutable {
                                static_assert(N % 16 == 0);
                                for (size_t o = 0; o < N; o += 16) {
                                    if (offset != 0 || o != 0) {
                                        pattern = _mm_shuffle_epi8(
                                                pattern, reshuffle);
                                    }
                                    _mm_storeu_si128(
                                            (__m128i_u*)(out + offset + o),
                                            pattern);
                                }
                            },
                            boundsCheck);
                }
            }
        } else {
            return copyWithSunkenBoundsCheck<N>(
                    len,
                    [out, in = out - offset](size_t offset) {
                        static_assert(N % 16 == 0);
                        for (size_t o = 0; o < N; o += 16) {
                            copy<16>(out + offset + o, in + offset + o);
                        }
                    },
                    boundsCheck);
        }
    }
#endif // ZL_ARCH_X86_64

#if ZL_ARCH_X86_64
    __attribute__((flatten)) static bool decodeFastInner(
            DecodeState* state,
            const DecodeInfo* info)
    {
        const ptrdiff_t seqLimit = info->numSeqs - kSeqSlop + 1;

        const ptrdiff_t outLimit = info->outCapacity - kOutSlop + 1;

        auto seq = state->seq;
        auto out = state->out;

        if (seq >= seqLimit || out >= outLimit) {
            return true;
        }

        const length_t* const extraLensLimit = state->extraLensLimit;
        const auto litsLimit                 = state->litsLimit;

        const auto outBase = info->outBase;
        auto extraLens     = state->extraLens;
        const auto tokens  = info->tokens;
        const auto offsets = info->offsets;
        auto lits          = state->lits;

        assert(extraLens < extraLensLimit);
        assert(lits < litsLimit);
        do {
#    define USE_DETOKENIZE 1
#    if USE_DETOKENIZE
            length_t lens[8];
            const int needsExtraMask =
                    detokenize4(ZL_readLE32(tokens + seq), extraLens, lens);
// #    pragma clang loop unroll(full)
#        if USE_LLVM_MCA
#            pragma clang loop unroll(disable)
#        endif
            // __builtin_prefetch(lits + 128);
            // __builtin_prefetch(tokens + 64);
            // __builtin_prefetch(offsets + 64);
            // __builtin_prefetch(extraLens + 64);
            for (size_t i = 0; i < 4; ++i) {
#        if USE_LLVM_MCA
                __asm volatile("# LLVM-MCA-BEGIN decodeFastInner" ::: "memory");
#        endif
                const ptrdiff_t ll     = lens[2 * i + 0];
                const ptrdiff_t ml     = lens[2 * i + 1];
                const ptrdiff_t offset = offsets[seq + i];
#    else
#        pragma clang loop unroll(full)
            for (size_t i = 0; i < 1; ++i) {
                auto ll = tokens[seq] & kLLMask;
                auto ml = (tokens[seq] >> kLLBits) & kMLMask;

                auto nextExtraLens = extraLens;

                if (ZL_UNLIKELY(ll == kLLMask)) {
                    ll += *nextExtraLens++;
                }
                if (ZL_UNLIKELY(ml == kMLMask)) {
                    ml += *nextExtraLens++;
                }
                const auto offset = offsets[seq];
#    endif

                // match starts at: 0 + ll - offset
                // match ends   at: 0 + ll - offset + ml - 1
                // lits start at  : 0
                //                : (ll - offset + ml - 1) < 0
                // safe if        : ll + ml - 1 < offset
                // safe if        : ll + ml <= offset
                // unsafe if      : ll + ml > offset

                const auto outMatch = out + ll;
                const auto outNext  = outMatch + ml;
                const auto litsNext = lits + ll;

                if (ZL_UNLIKELY(!copyWithSunkenBoundsCheck<kMaxLitLen>(
                            outBase + out, lits, ll, [=] {
                                return outMatch < outLimit
                                        && litsNext < litsLimit;
                            }))) {
                loop_break:
#    if USE_DETOKENIZE
                    seq += i;
                    extraLens += _mm_popcnt_u32(
                            needsExtraMask & ((1u << (2 * i)) - 1));
#    endif
                    goto exit;
                }

                const auto match = outMatch - offset;
                if (ZL_UNLIKELY(match < 0)) {
                    return false;
                }

                if (ZL_UNLIKELY(offset < ml)) {
                    if (ZL_UNLIKELY(!copyOverlappingWithSunkenBoundsCheck<
                                    kMaxMatchLen>(
                                outBase + outMatch, offset, ml, [=] {
                                    return outNext < outLimit;
                                }))) {
                        goto loop_break;
                    }
                } else {
                    if (ZL_UNLIKELY(!copyWithSunkenBoundsCheck<kMaxMatchLen>(
                                outBase + outMatch, outBase + match, ml, [=] {
                                    return outNext < outLimit;
                                }))) {
                        goto loop_break;
                    }
                }

                out  = outNext;
                lits = litsNext;
#    if !USE_DETOKENIZE
                extraLens = nextExtraLens;
                ++seq;
            }
#    else
#        if USE_LLVM_MCA
                __asm volatile("# LLVM-MCA-END decodeFastInner" ::: "memory");
#        endif
            }

            extraLens += _mm_popcnt_u32(needsExtraMask);
            seq += 4;
#    endif
        } while (seq < seqLimit && out < outLimit && extraLens < extraLensLimit
                 && lits < litsLimit);
    exit:
        assert(seq <= info->numSeqs);
        assert(out <= info->outCapacity);
        assert(extraLens <= state->extraLensEnd);
        assert(lits <= state->litsEnd);

        state->lits      = lits;
        state->out       = out;
        state->seq       = seq;
        state->extraLens = extraLens;

        return true;
    }
#endif // ZL_ARCH_X86_64

    static void copyLiterals(void* out, const void* lits, size_t len)
    {
        memcpy(out, lits, len);
    }

    static void copyMatch(uint8_t* out, ptrdiff_t offset, ptrdiff_t len)
    {
        if (offset >= len) {
            memcpy(out, out - offset, len);
        } else {
            for (ptrdiff_t i = 0; i < len; ++i) {
                out[i] = out[i - offset];
            }
        }
    }

#if ZL_ARCH_X86_64
    static bool bufferExtraLens(
            DecodeState* state,
            length_t extraLensBuffer[2 * kExtraLensSlop])
    {
        if (ZL_UNLIKELY(state->extraLens >= state->extraLensLimit)) {
            if (state->extraLens >= extraLensBuffer
                && state->extraLens < extraLensBuffer + 2 * kExtraLensSlop) {
                return false;
            }
            const size_t extraLensLeft = state->extraLensEnd - state->extraLens;
            assert(extraLensLeft <= kExtraLensSlop);
            if (state->extraLens != NULL) {
                memcpy(extraLensBuffer,
                       state->extraLens,
                       sizeof(state->extraLens[0]) * extraLensLeft);
            }
            state->extraLens      = extraLensBuffer;
            state->extraLensEnd   = extraLensBuffer + extraLensLeft;
            state->extraLensLimit = state->extraLensEnd + 1;
        }
        return true;
    }

    static bool bufferLits(
            DecodeState* state,
            uint8_t litsBuffer[2 * kLitsSlop])
    {
        if (ZL_UNLIKELY(state->lits >= state->litsLimit)) {
            if (state->lits >= litsBuffer
                && state->lits < litsBuffer + 2 * kLitsSlop) {
                return false;
            }
            const size_t litsLeft = state->litsEnd - state->lits;
            assert(litsLeft <= kLitsSlop);
            if (state->lits != NULL) {
                memcpy(litsBuffer, state->lits, litsLeft);
            }
            state->lits      = litsBuffer;
            state->litsEnd   = litsBuffer + litsLeft;
            state->litsLimit = state->litsEnd + 1;
        }
        return true;
    }

    static bool decodeFast(DecodeState* state, const DecodeInfo* info)
    {
        length_t extraLensBuffer[2 * kExtraLensSlop] = {};
        uint8_t litsBuffer[2 * kLitsSlop]            = {};
        while (state->seq < info->numSeqs) {
            if (!bufferExtraLens(state, extraLensBuffer)) {
                return false;
            }
            if (!bufferLits(state, litsBuffer)) {
                return false;
            }
            if (!decodeFastInner(state, info)) {
                return false;
            }
            if (state->seq == info->numSeqs) {
                break;
            }

            const auto token = info->tokens[state->seq];
            auto ll          = token & kLLMask;
            auto ml          = (token >> kLLBits) & kMLMask;

            auto out       = state->out;
            auto extraLens = state->extraLens;

            if (ll == kLLMask) {
                if (state->extraLens == state->extraLensEnd) {
                    return false;
                }
                ll += *extraLens++;
            }

            if (ml == kMLMask) {
                if (state->extraLens == state->extraLensEnd) {
                    return false;
                }
                ml += *extraLens++;
            }

            if (ll > (state->litsEnd - state->lits)) {
                return false;
            }
            if (ll > (info->outCapacity - out)) {
                return false;
            }
            copyLiterals(info->outBase + out, state->lits, ll);
            out += ll;

            const auto offset = info->offsets[state->seq];

            if (offset > out) {
                return false;
            }
            if (ml > (info->outCapacity - out)) {
                return false;
            }
            copyMatch(info->outBase + out, offset, ml);

            state->lits += ll;
            state->out       = out + ml;
            state->extraLens = extraLens;
            ++state->seq;
        }

        if (state->extraLens != state->extraLensEnd) {
            return false;
        }
        if (state->lits > state->litsEnd) {
            // can happen when using buffered literals
            return false;
        }

        const size_t ll = state->litsEnd - state->lits;
        if (ll > 0) {
            if (ll > (info->outCapacity - state->out)) {
                return false;
            }
            copyLiterals(info->outBase + state->out, state->lits, ll);
            state->out += ll;
        }
        return true;
    }
#endif // ZL_ARCH_X86_64

#if ZL_ARCH_X86_64
    static size_t decodeImplFast(
            uint8_t const* __restrict lits,
            size_t numLits,
            uint8_t const* __restrict tokens,
            offset_t const* __restrict offsets,
            size_t numSeqs,
            length_t const* __restrict extraLens,
            size_t numExtraLens,
            uint8_t* __restrict out,
            size_t outCapacity)
    {
        const auto extraLensEnd = extraLens + numExtraLens;
        const auto litsEnd      = lits + numLits;

        DecodeState state = {
            .lits           = lits,
            .litsLimit      = litsEnd - std::min<size_t>(numLits, kLitsSlop),
            .litsEnd        = litsEnd,
            .seq            = 0,
            .extraLens      = extraLens,
            .extraLensLimit = extraLensEnd
                    - std::min<size_t>(numExtraLens, kExtraLensSlop),
            .extraLensEnd = extraLensEnd,
            .out          = 0,
        };
        const DecodeInfo info = {
            .tokens      = tokens,
            .offsets     = offsets,
            .numSeqs     = (ptrdiff_t)numSeqs,
            .outCapacity = (ptrdiff_t)outCapacity,
            .outBase     = out,
        };
        if (!decodeFast(&state, &info)) {
            return 0;
        }
        return state.out;
    }
#endif // ZL_ARCH_X86_64

   public:
    static ZL_Report decode(
            ZL_Decoder* dictx,
            uint8_t const* __restrict lits,
            size_t numLits,
            uint8_t const* __restrict tokens,
            offset_t const* __restrict offsets,
            size_t numSeqs,
            length_t const* __restrict extraLens,
            size_t numExtraLens,
            uint8_t* __restrict out,
            size_t outCapacity,
            size_t llBits)
    {
        // fprintf(stderr, "numLits = %zu, numSeqs = %zu\n", numLits, numSeqs);
        ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
        auto impl = decodeImpl;
#if ZL_ARCH_X86_64
        if (llBits == 1) {
            impl = kQuartet ? Transform<kIsIndex, 1, 8 - 1>::decodeImplQuartet
                            : Transform<kIsIndex, 1, 8 - 1>::decodeImplFast;
        } else if (llBits == 2) {
            impl = kQuartet ? Transform<kIsIndex, 2, 8 - 2>::decodeImplQuartet
                            : Transform<kIsIndex, 2, 8 - 2>::decodeImplFast;
        } else if (llBits == 3) {
            impl = kQuartet ? Transform<kIsIndex, 3, 8 - 3>::decodeImplQuartet
                            : Transform<kIsIndex, 3, 8 - 3>::decodeImplFast;
        } else if (llBits == 4) {
            impl = kQuartet ? Transform<kIsIndex, 4, 8 - 4>::decodeImplQuartet
                            : Transform<kIsIndex, 4, 8 - 4>::decodeImplFast;
        } else if (llBits == 5) {
            impl = kQuartet ? Transform<kIsIndex, 5, 8 - 5>::decodeImplQuartet
                            : Transform<kIsIndex, 5, 8 - 5>::decodeImplFast;
        } else {
            ZL_ERR(GENERIC, "Unsupported LLBits %u", llBits);
        }
#else
        (void)llBits;
#endif // ZL_ARCH_X86_64
        size_t const size =
                impl(lits,
                     numLits,
                     tokens,
                     offsets,
                     numSeqs,
                     extraLens,
                     numExtraLens,
                     out,
                     outCapacity);
        return ZL_returnValue(size);
    }

    static ZL_Report decode(
            ZL_Decoder* dictx,
            ZL_Input const* inputs[]) noexcept
    {
        ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);

        auto lits      = inputs[0];
        auto tokens    = inputs[1];
        auto offsets   = inputs[2];
        auto extraLens = inputs[3];

        auto header      = ZL_Decoder_getCodecHeader(dictx);
        const uint8_t* h = (const uint8_t*)header.start;
        ZL_ERR_IF_LT(header.size, 2, corruption);
        auto llBits = *h & 0xf;
        ++h;
        ZL_TRY_LET_CONST(
                uint64_t, srcSize, ZL_varintDecode(&h, h + header.size - 1));

        ZL_Output* out = ZL_Decoder_create1OutStream(dictx, srcSize, 1);
        // auto impl = kIsIndex ? decodeImpl2 : decodeImpl;

        // #define decodeImplFast decodeImpl

        ZL_TRY_LET_CONST(
                size_t,
                size,
                decode(dictx,
                       (uint8_t const*)ZL_Input_ptr(lits),
                       ZL_Input_numElts(lits),
                       (uint8_t const*)ZL_Input_ptr(tokens),
                       (offset_t const*)ZL_Input_ptr(offsets),
                       ZL_Input_numElts(tokens),
                       (length_t const*)ZL_Input_ptr(extraLens),
                       ZL_Input_numElts(extraLens),
                       (uint8_t*)ZL_Output_ptr(out),
                       srcSize,
                       llBits));
        ZL_ERR_IF_ERR(ZL_Output_commit(out, srcSize));
        ZL_ERR_IF_NE(size, srcSize, corruption);

        return ZL_returnSuccess();
    }

    static std::array<ZL_Type, 4> constexpr kOutTypes = {
        ZL_Type_serial,
        ZL_Type_serial,
        ZL_Type_numeric,
        ZL_Type_numeric,
    };

    static ZL_TypedGraphDesc gd()
    {
        ZL_TypedGraphDesc desc = { .CTid = int(CustomCodecIDs::LZ)
                                           | (kIsIndex ? 1 : 0),
                                   .inStreamType   = ZL_Type_serial,
                                   .outStreamTypes = kOutTypes.data(),
                                   .nbOutStreams   = kOutTypes.size() };
        return desc;
    }

   public:
    static ZL_NodeID create(ZL_Compressor* cgraph, LzParameters params)
    {
        ZL_NodeID node = ZL_Compressor_getNode(cgraph, "lz_research.lz");
        if (node == ZL_NODE_ILLEGAL) {
            ZL_TypedEncoderDesc desc = {
                .gd          = gd(),
                .transform_f = encode,
                .name        = "!lz_research.lz",
            };
            node = ZL_Compressor_registerTypedEncoder(cgraph, &desc);
        }
        LocalParams lp;
        lp.addIntParam(0, params.llBits);
        const ZL_ParameterizedNodeDesc desc = {
            .node        = node,
            .localParams = lp.get(),
        };
        return ZL_Compressor_registerParameterizedNode(cgraph, &desc);
    }

    // static ZL_GraphID storeGraph(ZL_Compressor* cgraph)
    // {
    //     auto node                     = create(cgraph);
    //     std::array<ZL_GraphID, 4> out = {
    //         ZL_GRAPH_STORE, ZL_GRAPH_STORE, ZL_GRAPH_STORE,
    //         ZL_GRAPH_STORE
    //     };
    //     return ZL_Compressor_registerStaticGraph_fromNode(
    //             cgraph, node, out.data(), out.size());
    // }

    // static ZL_GraphID compressGraph(ZL_Compressor* cgraph, bool fast)
    // {
    //     std::array<ZL_GraphID, 2> q = { ZL_GRAPH_FSE, ZL_GRAPH_STORE };
    //     ZL_GraphID const quantize =
    //     ZL_Compressor_registerStaticGraph_fromNode(
    //             cgraph, ZL_NODE_QUANTIZE_LENGTHS, q.data(), q.size());
    //     auto node = create(cgraph);
    //     // std::array<ZL_GraphID, 4> out = {
    //     //     ZL_GRAPH_HUFFMAN, ZL_GRAPH_HUFFMAN, ZL_GRAPH_STORE,
    //     quantize}; std::array<ZL_GraphID, 4> out = {
    //         ZL_GRAPH_HUFFMAN,
    //         ZL_GRAPH_HUFFMAN,
    //         fast ? ZL_GRAPH_STORE : varByteGraph(cgraph),
    //         smallIntGraph(
    //                 cgraph, fast ? ZL_GRAPH_STORE : ZL_GRAPH_HUFFMAN,
    //                 quantize)
    //     };
    //     return ZL_Compressor_registerStaticGraph_fromNode(
    //             cgraph, node, out.data(), out.size());
    // }

    // static ZL_GraphID lz4Graph(ZL_Compressor* cgraph, bool huff)
    // {
    //     std::array<ZL_GraphID, 2> q = { ZL_GRAPH_FSE, ZL_GRAPH_STORE };
    //     ZL_Compressor_registerStaticGraph_fromNode(
    //             cgraph, ZL_NODE_QUANTIZE_LENGTHS, q.data(), q.size());
    //     auto node      = create(cgraph);
    //     auto rangePack = ZL_Compressor_registerStaticGraph_fromNode1o(
    //             cgraph, ZL_NODE_RANGE_PACK, ZL_GRAPH_STORE);
    //     // std::array<ZL_GraphID, 4> out = {
    //     //     ZL_GRAPH_STORE, ZL_GRAPH_STORE, ZL_GRAPH_STORE, quantize};
    //     std::array<ZL_GraphID, 4> out = {
    //         huff ? ZL_GRAPH_HUFFMAN : ZL_GRAPH_STORE,
    //         ZL_GRAPH_STORE,
    //         ZL_GRAPH_STORE,
    //         smallIntGraph(cgraph, ZL_GRAPH_STORE, rangePack)
    //     };
    //     return ZL_Compressor_registerStaticGraph_fromNode(
    //             cgraph, node, out.data(), out.size());
    // }

   public:
    static void registerDecoder(ZL_DCtx* dctx)
    {
        ZL_TypedDecoderDesc desc = {
            .gd          = gd(),
            .transform_f = decode,
            .name        = "!zl_research.lz",
        };
        ZL_REQUIRE_SUCCESS(ZL_DCtx_registerTypedDecoder(dctx, &desc));
    }

    // static char const* str(Mode mode)
    // {
    //     switch (mode) {
    //         case Mode::kStore:
    //             return "store";
    //             break;
    //         case Mode::kLz4:
    //             return "lz4";
    //             break;
    //         case Mode::kLz4Huf:
    //             return "lz4-huf";
    //             break;
    //         case Mode::kZstdFast:
    //             return "zstd-fast";
    //             break;
    //         case Mode::kZstdSlow:
    //             return "zstd-slow";
    //             break;
    //     };
    // }

    // static std::vector<uint8_t>
    // compress(uint8_t const* src, size_t srcSize, Mode mode, int level)
    // {
    //     std::vector<uint8_t> compressed(srcSize * 4);
    //     auto cgraph = ZL_Compressor_create();
    //     ZL_GraphID graph;
    //     switch (mode) {
    //         case Mode::kStore:
    //             graph = storeGraph(cgraph);
    //             break;
    //         case Mode::kLz4:
    //             graph = lz4Graph(cgraph, false);
    //             break;
    //         case Mode::kLz4Huf:
    //             graph = lz4Graph(cgraph, true);
    //             break;
    //         case Mode::kZstdFast:
    //             graph = compressGraph(cgraph, true);
    //             break;
    //         case Mode::kZstdSlow:
    //             graph = compressGraph(cgraph, false);
    //             break;
    //     };
    //     // auto graph = store ? storeGraph(cgraph) : lz4Graph(cgraph);
    //     ZL_REQUIRE_SUCCESS(ZL_Compressor_selectStartingGraphID(cgraph,
    //     graph)); ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
    //             cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION));
    //     ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
    //             cgraph, ZL_CParam_compressedChecksum,
    //             ZL_TernaryParam_disable));
    //     ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
    //             cgraph, ZL_CParam_contentChecksum,
    //             ZL_TernaryParam_disable));
    //     ZL_REQUIRE_SUCCESS(ZL_Compressor_setParameter(
    //             cgraph, ZL_CParam_compressionLevel, level));
    //     auto ret = ZL_compress_usingCompressor(
    //             compressed.data(), compressed.size(), src, srcSize,
    //             cgraph);
    //     ZL_REQUIRE_SUCCESS(ret);
    //     ZL_Compressor_free(cgraph);
    //     compressed.resize(ZL_validResult(ret));
    //     return compressed;
    // }

    // static void
    // benchmark(uint8_t const* src, size_t srcSize, Mode mode, int level)
    // {
    //     auto compressed = compress(src, srcSize, mode, level);
    //     std::vector<uint8_t> decompressed(srcSize);
    //     ZL_DCtx* dctx = ZL_DCtx_create();
    //     ZL_REQUIRE_SUCCESS(
    //             ZL_DCtx_setStreamArena(dctx, ZL_DataArenaType_stack));
    //     registerCustomTransforms(dctx);
    //     auto start = std::chrono::steady_clock::now();
    //     for (size_t i = 0; i < kIters; ++i) {
    //         ZL_REQUIRE_SUCCESS(ZL_DCtx_decompress(
    //                 dctx,
    //                 decompressed.data(),
    //                 decompressed.size(),
    //                 compressed.data(),
    //                 compressed.size()));
    //     }
    //     auto stop                         =
    //     std::chrono::steady_clock::now(); std::chrono::nanoseconds
    //     duration = stop - start; double const mbps  = (srcSize * kIters *
    //     1000.0) / duration.count(); double const ratio = double(srcSize)
    //     / compressed.size(); ZL_REQUIRE(!memcmp(src, decompressed.data(),
    //     srcSize)); fprintf(stderr,
    //             "llbits = %zu, mlbits = %zu, index = %u, mode = %9s,
    //             level = %d, ratio = %.2f, MB/s = %.2f\n", kLLBits,
    //             kMLBits, unsigned(kIsIndex ? 1 : 0), str(mode), level,
    //             ratio,
    //             mbps);
    //     ZL_DCtx_free(dctx);
    // }

    // static void write(
    //         char const* prefix,
    //         uint8_t const* src,
    //         size_t srcSize,
    //         Mode mode,
    //         int level)
    // {
    //     auto name = fmt::format(
    //             "{}.{}.{}.{}.{}.{}.zs",
    //             prefix,
    //             str(mode),
    //             level,
    //             kIsIndex ? 1 : 0,
    //             kLLBits,
    //             kMLBits);
    //     auto compressed = compress(src, srcSize, mode, level);
    //     ZL_REQUIRE(folly::writeFile(compressed, name.c_str()));
    // }
};

NodeID registerLz(Compressor& compressor, LzParameters params)
{
    return Transform<false, 0, 8>::create(compressor.get(), params);
}

void registerLz(DCtx& dctx)
{
    Transform<false, 0, 8>::registerDecoder(dctx.get());
}

class StandaloneTransform : public Transform<false, 0, 8> {
   public:
    StandaloneTransform() : Transform(nullptr) {}

    struct PtrSize {
        std::unique_ptr<uint8_t[]> ptr;
        size_t size;
        size_t width;
    };

    std::vector<PtrSize> ptrs_;
    std::string header_;

   private:
    virtual void* reserveStream(size_t idx, size_t eltCapacity, size_t eltWidth)
    {
        ptrs_.resize(std::max(ptrs_.size(), idx + 1));
        ptrs_[idx] = PtrSize{ std::make_unique_for_overwrite<uint8_t[]>(
                                      eltCapacity * eltWidth),
                              0,
                              eltWidth };
        return ptrs_[idx].ptr.get();
    }

    virtual ZL_Report commitStream(size_t idx, size_t eltCount)
    {
        assert(ptrs_[idx].size == 0);
        ptrs_[idx].size = eltCount;
        return ZL_returnSuccess();
    }

    virtual void writeHeader(poly::string_view header)
    {
        header_ = header;
    }
};

/* static */ size_t Lz4Like::compressBound(size_t srcSize)
{
    return 2 * srcSize;
}

/* static */ size_t Lz4Like::decompressedSize(poly::string_view src)
{
    if (src.size() < 4) {
        throw std::runtime_error("bad 1");
    }
    return ZL_readLE32(src.data());
}

static constexpr bool kEncodeExtraLens = true;

/* static */ size_t Lz4Like::compress(
        poly::span<char> dst,
        poly::string_view src)
{
    auto dstCapacity = dst.size();
    if (dst.size() < 4) {
        throw std::runtime_error("bad 2");
    }
    ZL_writeLE32(dst.data(), src.size());
    dst = dst.subspan(4);

    StandaloneTransform tr;
    if (ZL_isError(tr.encode(src, 1, 0))) {
        throw std::runtime_error("encoding failed");
    }

    if (dst.size() < 1 || tr.header_.size() < 1) {
        throw std::runtime_error("bad 3");
    }
    dst[0] = tr.header_[0];
    dst    = dst.subspan(1);

    auto writeStream = [&dst](const auto& stream) {
        if (dst.size() < 4 + stream.size * stream.width) {
            throw std::runtime_error("bad 4");
        }
        ZL_writeLE32(dst.data(), stream.size);
        memcpy(dst.data() + 4, stream.ptr.get(), stream.size * stream.width);
        dst = dst.subspan(4 + stream.size * stream.width);
    };

    if (tr.ptrs_.size() != 4) {
        throw std::runtime_error("error 2949");
    }

    if (kEncodeExtraLens) {
        auto extraLens = std::move(tr.ptrs_.back());
        tr.ptrs_.pop_back();

        StandaloneTransform::PtrSize largeExtraLens;
        largeExtraLens.ptr = std::make_unique_for_overwrite<uint8_t[]>(
                extraLens.size * sizeof(length_t));

        StandaloneTransform::PtrSize smallExtraLens;
        smallExtraLens.ptr =
                std::make_unique_for_overwrite<uint8_t[]>(extraLens.size);

        auto numLarge = SmallInt::encode(
                smallExtraLens.ptr.get(),
                (uint16_t*)largeExtraLens.ptr.get(),
                (const length_t*)extraLens.ptr.get(),
                extraLens.size);

        smallExtraLens.size  = extraLens.size;
        smallExtraLens.width = 1;

        largeExtraLens.size  = numLarge;
        largeExtraLens.width = sizeof(length_t);

        tr.ptrs_.push_back(std::move(smallExtraLens));
        tr.ptrs_.push_back(std::move(largeExtraLens));
    }

    for (const auto& stream : tr.ptrs_) {
        writeStream(stream);
    }

    return dstCapacity - dst.size();
}

/* static */ size_t Lz4Like::decompress(
        poly::span<char> dst,
        poly::string_view src)
{
    auto length = decompressedSize(src);
    if (dst.size() < length) {
        throw std::runtime_error("bad 5");
    }
    src = src.substr(4);
    if (src.empty()) {
        throw std::runtime_error("bad 6");
    }
    const auto llBits = src[0] & 0xF;
    src               = src.substr(1);

    struct Stream {
        const char* ptr;
        size_t size;
    };

    auto readStream = [&src](size_t width) {
        if (src.size() < 4) {
            throw std::runtime_error("bad 7");
        }
        auto size = ZL_readLE32(src.data());
        src       = src.substr(4);
        if (src.size() < width * size) {
            throw std::runtime_error("bad 8");
        }
        auto ptr = src.data();
        src      = src.substr(width * size);
        return Stream{ ptr, size };
    };

    auto lits    = readStream(1);
    auto tokens  = readStream(1);
    auto offsets = readStream(sizeof(offset_t));

    Stream extraLens;
    std::unique_ptr<length_t[]> extraLensBuffer;
    if constexpr (!kEncodeExtraLens) {
        extraLens = readStream(sizeof(length_t));
    } else {
        auto smallExtraLens = readStream(1);
        auto largeExtraLens = readStream(sizeof(length_t));

        extraLensBuffer =
                std::make_unique_for_overwrite<length_t[]>(smallExtraLens.size);
        SmallInt::decode(
                extraLensBuffer.get(),
                (const uint8_t*)smallExtraLens.ptr,
                smallExtraLens.size,
                (const length_t*)largeExtraLens.ptr,
                largeExtraLens.size);

        extraLens = Stream{ (const char*)extraLensBuffer.get(),
                            smallExtraLens.size };
    }

    auto report = StandaloneTransform::decode(
            nullptr,
            (const uint8_t*)lits.ptr,
            lits.size,
            (const uint8_t*)tokens.ptr,
            (const offset_t*)offsets.ptr,
            tokens.size,
            (const length_t*)extraLens.ptr,
            extraLens.size,
            (uint8_t*)dst.data(),
            dst.size(),
            llBits);
    if (ZL_isError(report) || ZL_validResult(report) != length) {
        throw std::runtime_error("bad 9");
    }
    return length;
}

} // namespace openzl::lz
