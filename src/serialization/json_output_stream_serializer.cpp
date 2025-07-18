// Copyright (c) 2012-2017, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2014-2018, The Monero Project
// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

#include "json_output_stream_serializer.h"
#include <cassert>
#include <stdexcept>
#include "common/string_tools.h"

using common::JsonValue;
using namespace mevacoin;

namespace mevacoin
{
    std::ostream &operator<<(std::ostream &out, const JsonOutputStreamSerializer &enumerator)
    {
        out << enumerator.root;
        return out;
    }
}

namespace
{

    template <typename T>
    void insertOrPush(JsonValue &js, common::StringView name, const T &value)
    {
        if (js.isArray())
        {
            js.pushBack(JsonValue(value));
        }
        else
        {
            js.insert(std::string(name), JsonValue(value));
        }
    }

}

JsonOutputStreamSerializer::JsonOutputStreamSerializer() : root(JsonValue::OBJECT)
{
    chain.push_back(&root);
}

JsonOutputStreamSerializer::~JsonOutputStreamSerializer()
{
}

ISerializer::SerializerType JsonOutputStreamSerializer::type() const
{
    return ISerializer::OUTPUT;
}

bool JsonOutputStreamSerializer::beginObject(common::StringView name)
{
    JsonValue &parent = *chain.back();
    JsonValue obj(JsonValue::OBJECT);

    if (parent.isObject())
    {
        chain.push_back(&parent.insert(std::string(name), obj));
    }
    else
    {
        chain.push_back(&parent.pushBack(obj));
    }

    return true;
}

void JsonOutputStreamSerializer::endObject()
{
    assert(!chain.empty());
    chain.pop_back();
}

bool JsonOutputStreamSerializer::beginArray(uint64_t &size, common::StringView name)
{
    JsonValue val(JsonValue::ARRAY);
    JsonValue &res = chain.back()->insert(std::string(name), val);
    chain.push_back(&res);
    return true;
}

void JsonOutputStreamSerializer::endArray()
{
    assert(!chain.empty());
    chain.pop_back();
}

bool JsonOutputStreamSerializer::operator()(uint64_t &value, common::StringView name)
{
    int64_t v = static_cast<int64_t>(value);
    return operator()(v, name);
}

bool JsonOutputStreamSerializer::operator()(uint16_t &value, common::StringView name)
{
    uint64_t v = static_cast<uint64_t>(value);
    return operator()(v, name);
}

bool JsonOutputStreamSerializer::operator()(int16_t &value, common::StringView name)
{
    int64_t v = static_cast<int64_t>(value);
    return operator()(v, name);
}

bool JsonOutputStreamSerializer::operator()(uint32_t &value, common::StringView name)
{
    uint64_t v = static_cast<uint64_t>(value);
    return operator()(v, name);
}

bool JsonOutputStreamSerializer::operator()(int32_t &value, common::StringView name)
{
    int64_t v = static_cast<int64_t>(value);
    return operator()(v, name);
}

bool JsonOutputStreamSerializer::operator()(int64_t &value, common::StringView name)
{
    insertOrPush(*chain.back(), name, value);
    return true;
}

bool JsonOutputStreamSerializer::operator()(double &value, common::StringView name)
{
    insertOrPush(*chain.back(), name, value);
    return true;
}

bool JsonOutputStreamSerializer::operator()(std::string &value, common::StringView name)
{
    insertOrPush(*chain.back(), name, value);
    return true;
}

bool JsonOutputStreamSerializer::operator()(uint8_t &value, common::StringView name)
{
    insertOrPush(*chain.back(), name, static_cast<int64_t>(value));
    return true;
}

bool JsonOutputStreamSerializer::operator()(bool &value, common::StringView name)
{
    insertOrPush(*chain.back(), name, value);
    return true;
}

bool JsonOutputStreamSerializer::binary(void *value, uint64_t size, common::StringView name)
{
    std::string hex = common::toHex(value, size);
    return (*this)(hex, name);
}

bool JsonOutputStreamSerializer::binary(std::string &value, common::StringView name)
{
    return binary(const_cast<char *>(value.data()), value.size(), name);
}
