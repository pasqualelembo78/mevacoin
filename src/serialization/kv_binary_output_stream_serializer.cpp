// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#include "kv_binary_output_stream_serializer.h"
#include "kv_binary_common.h"

#include <cassert>
#include <stdexcept>
#include <limits>
#include <common/stream_tools.h>

using namespace common;
using namespace mevacoin;

namespace
{

    template <typename T>
    void writePod(IOutputStream &s, const T &value)
    {
        write(s, &value, sizeof(T));
    }

    template <class T>
    uint64_t packVarint(IOutputStream &s, uint8_t type_or, uint64_t pv)
    {
        T v = static_cast<T>(pv << 2);
        v |= type_or;
        write(s, &v, sizeof(T));
        return sizeof(T);
    }

    void writeElementName(IOutputStream &s, common::StringView name)
    {
        if (name.getSize() > std::numeric_limits<uint8_t>::max())
        {
            throw std::runtime_error("Element name is too long");
        }

        uint8_t len = static_cast<uint8_t>(name.getSize());
        write(s, &len, sizeof(len));
        write(s, name.getData(), len);
    }

    uint64_t writeArraySize(IOutputStream &s, uint64_t val)
    {
        if (val <= 63)
        {
            return packVarint<uint8_t>(s, PORTABLE_RAW_SIZE_MARK_BYTE, val);
        }
        else if (val <= 16383)
        {
            return packVarint<uint16_t>(s, PORTABLE_RAW_SIZE_MARK_WORD, val);
        }
        else if (val <= 1073741823)
        {
            return packVarint<uint32_t>(s, PORTABLE_RAW_SIZE_MARK_DWORD, val);
        }
        else
        {
            if (val > 4611686018427387903)
            {
                throw std::runtime_error("failed to pack varint - too big amount");
            }
            return packVarint<uint64_t>(s, PORTABLE_RAW_SIZE_MARK_INT64, val);
        }
    }

}

namespace mevacoin
{

    KVBinaryOutputStreamSerializer::KVBinaryOutputStreamSerializer()
    {
        beginObject(std::string());
    }

    void KVBinaryOutputStreamSerializer::dump(IOutputStream &target)
    {
        assert(m_objectsStack.size() == 1);
        assert(m_stack.size() == 1);

        KVBinaryStorageBlockHeader hdr;
        hdr.m_signature_a = PORTABLE_STORAGE_SIGNATUREA;
        hdr.m_signature_b = PORTABLE_STORAGE_SIGNATUREB;
        hdr.m_ver = PORTABLE_STORAGE_FORMAT_VER;

        common::write(target, &hdr, sizeof(hdr));
        writeArraySize(target, m_stack.front().count);
        write(target, stream().data(), stream().size());
    }

    ISerializer::SerializerType KVBinaryOutputStreamSerializer::type() const
    {
        return ISerializer::OUTPUT;
    }

    bool KVBinaryOutputStreamSerializer::beginObject(common::StringView name)
    {
        checkArrayPreamble(BIN_KV_SERIALIZE_TYPE_OBJECT);

        m_stack.push_back(Level(name));
        m_objectsStack.push_back(MemoryStream());

        return true;
    }

    void KVBinaryOutputStreamSerializer::endObject()
    {
        assert(m_objectsStack.size());

        auto level = std::move(m_stack.back());
        m_stack.pop_back();

        auto objStream = std::move(m_objectsStack.back());
        m_objectsStack.pop_back();

        auto &out = stream();

        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_OBJECT, level.name);

        writeArraySize(out, level.count);
        write(out, objStream.data(), objStream.size());
    }

    bool KVBinaryOutputStreamSerializer::beginArray(uint64_t &size, common::StringView name)
    {
        m_stack.push_back(Level(name, size));
        return true;
    }

    void KVBinaryOutputStreamSerializer::endArray()
    {
        bool validArray = m_stack.back().state == State::Array;
        m_stack.pop_back();

        if (m_stack.back().state == State::Object && validArray)
        {
            ++m_stack.back().count;
        }
    }

    bool KVBinaryOutputStreamSerializer::operator()(uint8_t &value, common::StringView name)
    {
        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_UINT8, name);
        writePod(stream(), value);
        return true;
    }

    bool KVBinaryOutputStreamSerializer::operator()(uint16_t &value, common::StringView name)
    {
        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_UINT16, name);
        writePod(stream(), value);
        return true;
    }

    bool KVBinaryOutputStreamSerializer::operator()(int16_t &value, common::StringView name)
    {
        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_INT16, name);
        writePod(stream(), value);
        return true;
    }

    bool KVBinaryOutputStreamSerializer::operator()(uint32_t &value, common::StringView name)
    {
        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_UINT32, name);
        writePod(stream(), value);
        return true;
    }

    bool KVBinaryOutputStreamSerializer::operator()(int32_t &value, common::StringView name)
    {
        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_INT32, name);
        writePod(stream(), value);
        return true;
    }

    bool KVBinaryOutputStreamSerializer::operator()(int64_t &value, common::StringView name)
    {
        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_INT64, name);
        writePod(stream(), value);
        return true;
    }

    bool KVBinaryOutputStreamSerializer::operator()(uint64_t &value, common::StringView name)
    {
        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_UINT64, name);
        writePod(stream(), value);
        return true;
    }

    bool KVBinaryOutputStreamSerializer::operator()(bool &value, common::StringView name)
    {
        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_BOOL, name);
        writePod(stream(), value);
        return true;
    }

    bool KVBinaryOutputStreamSerializer::operator()(double &value, common::StringView name)
    {
        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_DOUBLE, name);
        writePod(stream(), value);
        return true;
    }

    bool KVBinaryOutputStreamSerializer::operator()(std::string &value, common::StringView name)
    {
        writeElementPrefix(BIN_KV_SERIALIZE_TYPE_STRING, name);

        auto &out = stream();
        writeArraySize(out, value.size());
        write(out, value.data(), value.size());
        return true;
    }

    bool KVBinaryOutputStreamSerializer::binary(void *value, uint64_t size, common::StringView name)
    {
        if (size > 0)
        {
            writeElementPrefix(BIN_KV_SERIALIZE_TYPE_STRING, name);
            auto &out = stream();
            writeArraySize(out, size);
            write(out, value, size);
        }
        return true;
    }

    bool KVBinaryOutputStreamSerializer::binary(std::string &value, common::StringView name)
    {
        return binary(const_cast<char *>(value.data()), value.size(), name);
    }

    void KVBinaryOutputStreamSerializer::writeElementPrefix(uint8_t type, common::StringView name)
    {
        assert(m_stack.size());

        checkArrayPreamble(type);
        Level &level = m_stack.back();

        if (level.state != State::Array)
        {
            if (!name.isEmpty())
            {
                auto &s = stream();
                writeElementName(s, name);
                write(s, &type, 1);
            }
            ++level.count;
        }
    }

    void KVBinaryOutputStreamSerializer::checkArrayPreamble(uint8_t type)
    {
        if (m_stack.empty())
        {
            return;
        }

        Level &level = m_stack.back();

        if (level.state == State::ArrayPrefix)
        {
            auto &s = stream();
            writeElementName(s, level.name);
            char c = BIN_KV_SERIALIZE_FLAG_ARRAY | type;
            write(s, &c, 1);
            writeArraySize(s, level.count);
            level.state = State::Array;
        }
    }

    MemoryStream &KVBinaryOutputStreamSerializer::stream()
    {
        assert(m_objectsStack.size());
        return m_objectsStack.back();
    }

}
