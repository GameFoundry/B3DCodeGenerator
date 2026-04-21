//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#pragma once

#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>

/**
 * Minimal JSON writer that streams output to an llvm::raw_ostream. Supports nested objects/arrays, basic value
 * types, optional pretty printing with indentation, and proper escaping for string values. Used by the doc-gen
 * emitter to produce a documentation JSON file. No JSON parsing is provided; this is output-only.
 */
class JsonWriter
{
public:
	explicit JsonWriter(llvm::raw_ostream& out, bool pretty = true)
		: mOut(out), mPretty(pretty)
	{ }

	void BeginObject()
	{
		StartValue();
		mOut << '{';
		mState.push_back(State{ /*isObject*/ true, /*hasItems*/ false });
	}

	void EndObject()
	{
		assert(!mState.empty() && mState.back().IsObject);
		const bool hadItems = mState.back().HasItems;
		mState.pop_back();
		if (hadItems)
			WriteNewlineAndIndent();
		mOut << '}';
	}

	void BeginArray()
	{
		StartValue();
		mOut << '[';
		mState.push_back(State{ /*isObject*/ false, /*hasItems*/ false });
	}

	void EndArray()
	{
		assert(!mState.empty() && !mState.back().IsObject);
		const bool hadItems = mState.back().HasItems;
		mState.pop_back();
		if (hadItems)
			WriteNewlineAndIndent();
		mOut << ']';
	}

	/** Writes a JSON object key. The next call must write a value. */
	void Key(llvm::StringRef name)
	{
		assert(!mState.empty() && mState.back().IsObject);
		if (mState.back().HasItems)
			mOut << ',';
		WriteNewlineAndIndent();
		WriteEscapedString(name);
		mOut << (mPretty ? ": " : ":");
		mAfterKey = true;
	}

	void String(llvm::StringRef value)
	{
		StartValue();
		WriteEscapedString(value);
	}

	void Integer(int64_t value)
	{
		StartValue();
		mOut << value;
	}

	void Boolean(bool value)
	{
		StartValue();
		mOut << (value ? "true" : "false");
	}

	void Null()
	{
		StartValue();
		mOut << "null";
	}

	// Convenience helpers for common key+value patterns.
	void StringField(llvm::StringRef key, llvm::StringRef value) { Key(key); String(value); }
	void IntField(llvm::StringRef key, int64_t value)            { Key(key); Integer(value); }
	void BoolField(llvm::StringRef key, bool value)              { Key(key); Boolean(value); }
	void NullField(llvm::StringRef key)                          { Key(key); Null(); }

	/** Writes either a string value or null, based on whether the value is empty. */
	void OptionalStringField(llvm::StringRef key, llvm::StringRef value)
	{
		Key(key);
		if (value.empty())
			Null();
		else
			String(value);
	}

private:
	struct State
	{
		bool IsObject;
		bool HasItems;
	};

	void StartValue()
	{
		if (mAfterKey)
		{
			mAfterKey = false;
			if (!mState.empty())
				mState.back().HasItems = true;
			return;
		}

		if (mState.empty())
			return; // Top-level value.

		assert(!mState.back().IsObject); // Bare value inside an object requires a key first.
		if (mState.back().HasItems)
			mOut << ',';
		WriteNewlineAndIndent();
		mState.back().HasItems = true;
	}

	void WriteNewlineAndIndent()
	{
		if (!mPretty)
			return;
		mOut << '\n';
		for (size_t i = 0; i < mState.size(); ++i)
			mOut << '\t';
	}

	void WriteEscapedString(llvm::StringRef value)
	{
		mOut << '"';
		for (size_t i = 0; i < value.size(); ++i)
		{
			const unsigned char c = static_cast<unsigned char>(value[i]);
			switch (c)
			{
			case '"':  mOut << "\\\""; break;
			case '\\': mOut << "\\\\"; break;
			case '\b': mOut << "\\b"; break;
			case '\f': mOut << "\\f"; break;
			case '\n': mOut << "\\n"; break;
			case '\r': mOut << "\\r"; break;
			case '\t': mOut << "\\t"; break;
			default:
				if (c < 0x20)
				{
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", c);
					mOut << buf;
				}
				else
				{
					mOut << static_cast<char>(c);
				}
				break;
			}
		}
		mOut << '"';
	}

	llvm::raw_ostream& mOut;
	bool mPretty;
	llvm::SmallVector<State, 16> mState;
	bool mAfterKey = false;
};
