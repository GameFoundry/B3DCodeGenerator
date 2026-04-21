//************************************* B3D Framework - Copyright 2026 Marko Pintera *************************************//
//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//
#include "B3DDocGenVisitor.h"

#include "B3DCommentParser.h"
#include "B3DJsonWriter.h"
#include "B3DParserUtility.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <cctype>
#include <cstring>

namespace
{
	/** Returns a Clang printing policy tuned for documentation output: prints fully qualified names and avoids the "class"/"struct" tag prefix on type names. */
	PrintingPolicy MakeDocPrintingPolicy(const ASTContext& ctx)
	{
		PrintingPolicy policy = ctx.getPrintingPolicy();
		policy.SuppressTagKeyword = true;
		policy.SuppressUnwrittenScope = true;
		policy.AnonymousTagLocations = false;
		policy.TerseOutput = true;
		policy.PolishForDeclaration = true;

		return policy;
	}

	/** Removes any trailing "::" from a namespace path string. */
	std::string StripTrailingColons(const std::string& namespacePathString)
	{
		if (namespacePathString.size() >= 2 && namespacePathString[namespacePathString.size() - 1] == ':' && namespacePathString[namespacePathString.size() - 2] == ':')
			return namespacePathString.substr(0, namespacePathString.size() - 2);

		return namespacePathString;
	}

	/** Trims leading and trailing whitespace from the provided string. */
	std::string TrimWhitespace(const std::string& input)
	{
		size_t begin = 0;
		while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])))
			++begin;

		size_t end = input.size();
		while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])))
			--end;

		return input.substr(begin, end - begin);
	}

	/** Collapses runs of whitespace (including embedded newlines) into a single space, then trims. */
	std::string CollapseWhitespace(const std::string& input)
	{
		std::string result;
		result.reserve(input.size());
		bool prevSpace = false;
		for (char c : input)
		{
			if (std::isspace(static_cast<unsigned char>(c)))
			{
				if (!prevSpace)
				{
					result.push_back(' ');
					prevSpace = true;
				}
			}
			else
			{
				result.push_back(c);
				prevSpace = false;
			}
		}

		return TrimWhitespace(result);
	}

	/**
	 * Strips `<type-parameter-N-M, ...>` and `<value-parameter-N-M, ...>` placeholder blocks that Clang emits when
	 * pretty-printing members of a non-instantiated class template (notably constructors and destructors, whose printed
	 * name includes the injected-class-type template argument list). The placeholders add no information for the reader
	 * and break anchor generation downstream, so we scrub them uniformly from names and signatures.
	 */
	std::string StripUnresolvedTemplateParams(const std::string& input)
	{
		if (input.find('<') == std::string::npos)
			return input;
		std::string result;
		result.reserve(input.size());
		size_t i = 0;
		while (i < input.size())
		{
			if (input[i] != '<')
			{
				result.push_back(input[i]);
				++i;
				continue;
			}
			size_t depth = 1;
			size_t j = i + 1;
			while (j < input.size() && depth > 0)
			{
				if (input[j] == '<') ++depth;
				else if (input[j] == '>')
				{
					--depth;
					if (depth == 0) break;
				}
				++j;
			}
			if (j >= input.size() || depth != 0)
			{
				result.push_back(input[i]);
				++i;
				continue;
			}
			const std::string inner = input.substr(i + 1, j - i - 1);
			bool allPlaceholders = !inner.empty();
			size_t pos = 0;
			while (pos < inner.size())
			{
				while (pos < inner.size() && std::isspace(static_cast<unsigned char>(inner[pos])))
					++pos;
				const char* kinds[] = { "type-parameter-", "value-parameter-" };
				bool matched = false;
				for (const char* kind : kinds)
				{
					const size_t kindLen = std::strlen(kind);
					if (pos + kindLen > inner.size())
						continue;
					if (inner.compare(pos, kindLen, kind) != 0)
						continue;
					size_t k = pos + kindLen;
					size_t digits = 0;
					while (k < inner.size() && std::isdigit(static_cast<unsigned char>(inner[k]))) { ++k; ++digits; }
					if (digits == 0 || k >= inner.size() || inner[k] != '-')
						break;
					++k;
					digits = 0;
					while (k < inner.size() && std::isdigit(static_cast<unsigned char>(inner[k]))) { ++k; ++digits; }
					if (digits == 0)
						break;
					pos = k;
					matched = true;
					break;
				}
				if (!matched)
				{
					allPlaceholders = false;
					break;
				}
				while (pos < inner.size() && std::isspace(static_cast<unsigned char>(inner[pos])))
					++pos;
				if (pos < inner.size())
				{
					if (inner[pos] != ',')
					{
						allPlaceholders = false;
						break;
					}
					++pos;
				}
			}
			if (allPlaceholders)
				i = j + 1;
			else
			{
				result.push_back(input[i]);
				++i;
			}
		}
		return result;
	}

	/** Resolves a source location to a "file" + "line" pair via the source manager. Returns an empty filename if the location is invalid. */
	void GetPresumedFileAndLine(const SourceManager& sm, SourceLocation loc, std::string& outFile, unsigned& outLine)
	{
		outFile.clear();
		outLine = 0;
		if (loc.isInvalid())
			return;

		PresumedLoc presumed = sm.getPresumedLoc(loc);
		if (presumed.isInvalid())
			return;

		const char* filename = presumed.getFilename();
		outFile = filename != nullptr ? filename : "";
		outLine = presumed.getLine();
	}
}

bool BansheeDocGeneratorASTVisitor::VisitCXXRecordDecl(CXXRecordDecl* recordDeclaration)
{
	if (recordDeclaration == nullptr)
		return true;

	if (!recordDeclaration->isCompleteDefinition())
		return true;

	if (recordDeclaration->isImplicit())
		return true;

	if (recordDeclaration->isLambda())
		return true;

	if (recordDeclaration->getIdentifier() == nullptr)
		return true; // Anonymous record.

	if (IsInSystemHeader(recordDeclaration))
		return true;

	const Decl* canonicalDeclaration = recordDeclaration->getCanonicalDecl();
	if (!mEmittedDecls.insert(canonicalDeclaration).second)
		return true;

	const bool isStruct = recordDeclaration->isStruct() || recordDeclaration->isUnion();
	const std::string parentQualifiedName = recordDeclaration->getQualifiedNameAsString();

	{
		std::string buffer;
		raw_string_ostream stream(buffer);
		JsonWriter writer(stream);
		WriteClassObject(recordDeclaration, writer, isStruct);
		stream.flush();
		mClasses.push_back(std::move(buffer));
	}

	for (auto it = recordDeclaration->method_begin(); it != recordDeclaration->method_end(); ++it)
	{
		const CXXMethodDecl* methodDeclaration = *it;
		if (methodDeclaration == nullptr)
			continue;

		if (methodDeclaration->isImplicit())
			continue;

		if (methodDeclaration->isDeleted())
			continue;

		std::string buffer;
		raw_string_ostream stream(buffer);
		JsonWriter writer(stream);
		WriteMethodMember(methodDeclaration, parentQualifiedName, writer);
		stream.flush();
		mMembers.push_back(std::move(buffer));
	}

	for (auto it = recordDeclaration->field_begin(); it != recordDeclaration->field_end(); ++it)
	{
		const FieldDecl* fieldDeclaration = *it;
		if (fieldDeclaration == nullptr)
			continue;

		if (fieldDeclaration->isImplicit())
			continue;

		if (fieldDeclaration->getIdentifier() == nullptr)
			continue; // Anonymous bitfield.

		std::string buffer;
		raw_string_ostream stream(buffer);
		JsonWriter writer(stream);
		WriteFieldMember(fieldDeclaration, parentQualifiedName, writer);
		stream.flush();
		mMembers.push_back(std::move(buffer));
	}

	return true;
}

bool BansheeDocGeneratorASTVisitor::VisitEnumDecl(EnumDecl* enumDeclaration)
{
	if (enumDeclaration == nullptr)
		return true;

	if (enumDeclaration->isImplicit())
		return true;

	if (enumDeclaration->getIdentifier() == nullptr)
		return true;

	if (IsInSystemHeader(enumDeclaration))
		return true;

	const Decl* canonical = enumDeclaration->getCanonicalDecl();
	if (!mEmittedDecls.insert(canonical).second)
		return true;

	std::string buffer;
	raw_string_ostream stream(buffer);
	JsonWriter writer(stream);
	WriteEnumObject(enumDeclaration, writer);
	stream.flush();
	mEnums.push_back(std::move(buffer));

	return true;
}

bool BansheeDocGeneratorASTVisitor::VisitFunctionDecl(FunctionDecl* functionDeclaration)
{
	if (functionDeclaration == nullptr)
		return true;

	if (functionDeclaration->isImplicit())
		return true;

	if (isa<CXXMethodDecl>(functionDeclaration))
		return true; // Methods are emitted via VisitCXXRecordDecl.

	if (functionDeclaration->getIdentifier() == nullptr)
		return true; // Operators and conversion functions: skip for v1.

	if (functionDeclaration->isFunctionTemplateSpecialization())
		return true;

	if (IsInSystemHeader(functionDeclaration))
		return true;

	const Decl* canonical = functionDeclaration->getCanonicalDecl();
	if (!mEmittedDecls.insert(canonical).second)
		return true;

	std::string buffer;
	raw_string_ostream stream(buffer);
	JsonWriter writer(stream);
	WriteFunctionObject(functionDeclaration, writer);
	stream.flush();
	mFunctions.push_back(std::move(buffer));

	return true;
}

bool BansheeDocGeneratorASTVisitor::WriteJSON(StringRef outputPath)
{
	std::error_code errorCode;
	raw_fd_ostream out(outputPath, errorCode, sys::fs::OF_Text);
	if (errorCode)
	{
		errs() << "Error: Failed to open docgen output file \"" << outputPath << "\": " << errorCode.message() << "\n";
		return false;
	}

	// Write the top-level envelope manually so we can splice pre-serialized JSON object strings directly
	// into each array without running them through the writer (which would re-escape them).
	auto fnSpliceArray = [&out](StringRef key, const std::vector<std::string>& items)
	{
		out << ",\n\t\"" << key << "\": ";
		if (items.empty())
		{
			out << "[]";
			return;
		}
		out << "[";
		for (size_t i = 0; i < items.size(); ++i)
		{
			if (i > 0)
				out << ",";
			out << "\n\t\t" << items[i];
		}
		out << "\n\t]";
	};

	out << "{\n";
	out << "\t\"version\": 1";
	out << ",\n\t\"generator\": \"BansheeCodeGenerator\"";
	fnSpliceArray("classes", mClasses);
	fnSpliceArray("members", mMembers);
	fnSpliceArray("enums", mEnums);
	fnSpliceArray("functions", mFunctions);
	out << "\n}\n";

	out.flush();
	return true;
}

void BansheeDocGeneratorASTVisitor::WriteClassObject(const CXXRecordDecl* recordDeclaration, JsonWriter& writer, bool isStruct)
{
	writer.BeginObject();
	writer.StringField("kind", isStruct ? "struct" : "class");
	writer.StringField("name", recordDeclaration->getNameAsString());
	writer.StringField("qualified_name", recordDeclaration->getQualifiedNameAsString());
	writer.StringField("namespace", StripTrailingColons(ParserUtility::GetNamespace(recordDeclaration)));

	const ClassTemplateDecl* templateDeclaration = recordDeclaration->getDescribedClassTemplate();
	if (templateDeclaration != nullptr)
	{
		writer.Key("template_params");
		WriteTemplateParams(templateDeclaration->getTemplateParameters(), writer);
	}
	else
	{
		writer.NullField("template_params");
	}

	writer.Key("bases");
	WriteBaseList(recordDeclaration, writer);

	writer.StringField("visibility", VisibilityToString(recordDeclaration->getAccess()));

	writer.Key("location");
	WriteLocation(recordDeclaration, writer);

	writer.Key("doc");
	CommentEntry entry;
	mCommentParser.ParseComments(recordDeclaration, entry, CommentParseMode::PreserveFormatting);
	WriteCommentEntry(entry, writer);

	writer.EndObject();
}

void BansheeDocGeneratorASTVisitor::WriteMethodMember(const CXXMethodDecl* methodDeclaration, StringRef parentQualifiedName, JsonWriter& writer)
{
	writer.BeginObject();
	writer.StringField("kind", "method");
	writer.StringField("name", StripUnresolvedTemplateParams(methodDeclaration->getNameAsString()));
	writer.StringField("qualified_name", StripUnresolvedTemplateParams(methodDeclaration->getQualifiedNameAsString()));
	writer.StringField("parent_class_qname", parentQualifiedName);
	writer.StringField("signature", StripUnresolvedTemplateParams(PrettyPrintSignature(methodDeclaration)));

	const bool isCtor = isa<CXXConstructorDecl>(methodDeclaration);
	const bool isDtor = isa<CXXDestructorDecl>(methodDeclaration);

	if (isCtor || isDtor)
		writer.NullField("return_type");
	else
		writer.StringField("return_type", TypeToString(methodDeclaration->getReturnType()));

	writer.Key("param_list");
	WriteParamList(methodDeclaration, writer);

	const FunctionTemplateDecl* functionTemplateDeclaration = methodDeclaration->getDescribedFunctionTemplate();
	if (functionTemplateDeclaration != nullptr)
	{
		writer.Key("template_params");
		WriteTemplateParams(functionTemplateDeclaration->getTemplateParameters(), writer);
	}
	else
	{
		writer.NullField("template_params");
	}

	writer.StringField("visibility", VisibilityToString(methodDeclaration->getAccess()));
	writer.BoolField("is_static", methodDeclaration->isStatic());
	writer.BoolField("is_virtual", methodDeclaration->isVirtual());
	writer.BoolField("is_const", methodDeclaration->isConst());
	writer.BoolField("is_constructor", isCtor);
	writer.BoolField("is_destructor", isDtor);
	writer.BoolField("is_override", methodDeclaration->hasAttr<OverrideAttr>());
	writer.BoolField("is_final", methodDeclaration->hasAttr<FinalAttr>());
	writer.BoolField("is_constexpr", methodDeclaration->isConstexpr());
	writer.NullField("default_value");

	writer.Key("location");
	WriteLocation(methodDeclaration, writer);

	writer.Key("doc");
	CommentEntry entry;
	mCommentParser.ParseComments(methodDeclaration, entry, CommentParseMode::PreserveFormatting);
	WriteCommentEntry(entry, writer);

	writer.EndObject();
}

void BansheeDocGeneratorASTVisitor::WriteFieldMember(const FieldDecl* fieldDeclaration, StringRef parentQualifiedName, JsonWriter& writer)
{
	writer.BeginObject();
	writer.StringField("kind", "field");
	writer.StringField("name", fieldDeclaration->getNameAsString());
	writer.StringField("qualified_name", fieldDeclaration->getQualifiedNameAsString());
	writer.StringField("parent_class_qname", parentQualifiedName);

	const std::string typeString = TypeToString(fieldDeclaration->getType());
	const std::string signature = typeString + " " + fieldDeclaration->getNameAsString();
	writer.StringField("signature", signature);
	writer.StringField("return_type", typeString);
	writer.NullField("template_params");

	writer.StringField("visibility", VisibilityToString(fieldDeclaration->getAccess()));
	writer.BoolField("is_static", false);
	writer.BoolField("is_const", fieldDeclaration->getType().isConstQualified());
	writer.BoolField("is_mutable", fieldDeclaration->isMutable());

	if (fieldDeclaration->hasInClassInitializer())
	{
		const Expr* init = fieldDeclaration->getInClassInitializer();
		writer.StringField("default_value", ExpressionToString(init));
	}
	else
	{
		writer.NullField("default_value");
	}

	writer.Key("location");
	WriteLocation(fieldDeclaration, writer);

	writer.Key("doc");
	CommentEntry entry;
	mCommentParser.ParseComments(fieldDeclaration, entry, CommentParseMode::PreserveFormatting);
	WriteCommentEntry(entry, writer);

	writer.EndObject();
}

void BansheeDocGeneratorASTVisitor::WriteEnumObject(const EnumDecl* enumDeclaration, JsonWriter& writer)
{
	writer.BeginObject();
	writer.StringField("kind", "enum");
	writer.StringField("name", enumDeclaration->getNameAsString());
	writer.StringField("qualified_name", enumDeclaration->getQualifiedNameAsString());
	writer.StringField("namespace", StripTrailingColons(ParserUtility::GetNamespace(enumDeclaration)));
	writer.BoolField("is_enum_class", enumDeclaration->isScoped());

	if (enumDeclaration->getIntegerType().isNull())
		writer.NullField("enum_underlying");
	else
		writer.StringField("enum_underlying", TypeToString(enumDeclaration->getIntegerType()));

	writer.StringField("visibility", VisibilityToString(enumDeclaration->getAccess()));

	writer.Key("location");
	WriteLocation(enumDeclaration, writer);

	writer.Key("enum_values");
	writer.BeginArray();
	for (auto it = enumDeclaration->enumerator_begin(); it != enumDeclaration->enumerator_end(); ++it)
	{
		const EnumConstantDecl* enumConstantDeclaration = *it;
		writer.BeginObject();
		writer.StringField("name", enumConstantDeclaration->getNameAsString());

		llvm::SmallString<32> enumValueString;
		enumConstantDeclaration->getInitVal().toString(enumValueString, 10);
		writer.StringField("value", llvm::StringRef(enumValueString.data(), enumValueString.size()));

		writer.Key("doc");
		CommentEntry entry;
		mCommentParser.ParseComments(enumConstantDeclaration, entry, CommentParseMode::PreserveFormatting);
		WriteCommentEntry(entry, writer);
		writer.EndObject();
	}
	writer.EndArray();

	writer.Key("doc");
	CommentEntry entry;
	mCommentParser.ParseComments(enumDeclaration, entry, CommentParseMode::PreserveFormatting);
	WriteCommentEntry(entry, writer);

	writer.EndObject();
}

void BansheeDocGeneratorASTVisitor::WriteFunctionObject(const FunctionDecl* functionDeclaration, JsonWriter& writer)
{
	writer.BeginObject();
	writer.StringField("kind", "function");
	writer.StringField("name", functionDeclaration->getNameAsString());
	writer.StringField("qualified_name", functionDeclaration->getQualifiedNameAsString());
	writer.StringField("namespace", StripTrailingColons(ParserUtility::GetNamespace(functionDeclaration)));
	writer.StringField("signature", PrettyPrintSignature(functionDeclaration));
	writer.StringField("return_type", TypeToString(functionDeclaration->getReturnType()));

	writer.Key("param_list");
	WriteParamList(functionDeclaration, writer);

	const FunctionTemplateDecl* functionTemplateDeclaration = functionDeclaration->getDescribedFunctionTemplate();
	if (functionTemplateDeclaration != nullptr)
	{
		writer.Key("template_params");
		WriteTemplateParams(functionTemplateDeclaration->getTemplateParameters(), writer);
	}
	else
	{
		writer.NullField("template_params");
	}

	writer.BoolField("is_constexpr", functionDeclaration->isConstexpr());
	writer.BoolField("is_inline", functionDeclaration->isInlined());

	writer.Key("location");
	WriteLocation(functionDeclaration, writer);

	writer.Key("doc");

	CommentEntry entry;
	mCommentParser.ParseComments(functionDeclaration, entry, CommentParseMode::PreserveFormatting);
	WriteCommentEntry(entry, writer);

	writer.EndObject();
}

void BansheeDocGeneratorASTVisitor::WriteCommentEntry(const CommentEntry& entry, JsonWriter& writer)
{
	writer.BeginObject();

	const std::string copydocTarget = DetectCopydocTarget(entry);
	if (!copydocTarget.empty())
	{
		writer.StringField("brief", "");
		writer.StringField("description", "");
		writer.StringField("copydoc_target", copydocTarget);
	}
	else
	{
		const std::string flattenedComment = FlattenComment(entry.Brief);
		// Auto-brief: first sentence becomes brief, remainder becomes description. A sentence ends at
		// '.' followed by whitespace only when the next non-whitespace character is uppercase (or the
		// string ends there) — this avoids splitting common abbreviations like "i.e." and "e.g." where
		// the continuation is lowercase.
		size_t splitPosition = std::string::npos;
		for (size_t characterIndex = 0; characterIndex + 1 < flattenedComment.size(); ++characterIndex)
		{
			if (flattenedComment[characterIndex] != '.')
				continue;
			if (flattenedComment[characterIndex + 1] != ' ' && flattenedComment[characterIndex + 1] != '\n')
				continue;
			size_t next = characterIndex + 1;
			while (next < flattenedComment.size() && std::isspace(static_cast<unsigned char>(flattenedComment[next])))
				++next;
			if (next >= flattenedComment.size() || std::isupper(static_cast<unsigned char>(flattenedComment[next])))
			{
				splitPosition = characterIndex + 1;
				break;
			}
		}
		if (splitPosition == std::string::npos)
		{
			writer.StringField("brief", flattenedComment);
			writer.StringField("description", "");
		}
		else
		{
			writer.StringField("brief", TrimWhitespace(flattenedComment.substr(0, splitPosition)));
			writer.StringField("description", TrimWhitespace(flattenedComment.substr(splitPosition)));
		}

		writer.NullField("copydoc_target");
	}

	writer.Key("params");
	writer.BeginArray();
	for (const CommentParameterEntry& parameterComment : entry.ParameterComments)
	{
		writer.BeginArray();
		writer.String(parameterComment.Name);
		writer.String(FlattenComment(parameterComment.Comments));
		writer.EndArray();
	}
	writer.EndArray();

	writer.Key("template_params_doc");
	writer.BeginArray();
	for (const CommentParameterEntry& templateParameterComment : entry.TemplateParameterComments)
	{
		writer.BeginArray();
		writer.String(templateParameterComment.Name);
		writer.String(FlattenComment(templateParameterComment.Comments));
		writer.EndArray();
	}
	writer.EndArray();

	writer.StringField("returns", FlattenComment(entry.ReturnValueComments));

	writer.EndObject();
}

void BansheeDocGeneratorASTVisitor::WriteLocation(const Decl* decl, JsonWriter& writer)
{
	std::string file;
	unsigned line = 0;
	GetPresumedFileAndLine(mASTContext->getSourceManager(), decl->getBeginLoc(), file, line);

	writer.BeginObject();
	writer.StringField("file", file);
	writer.IntField("line", static_cast<int64_t>(line));
	writer.EndObject();
}

void BansheeDocGeneratorASTVisitor::WriteParamList(const FunctionDecl* decl, JsonWriter& writer)
{
	writer.BeginArray();

	const unsigned parameterCount = decl->getNumParams();
	for (unsigned parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex)
	{
		const ParmVarDecl* parameter = decl->getParamDecl(parameterIndex);
		writer.BeginArray();
		writer.String(TypeToString(parameter->getType()));
		writer.String(parameter->getNameAsString());
		writer.EndArray();
	}

	writer.EndArray();
}

void BansheeDocGeneratorASTVisitor::WriteBaseList(const CXXRecordDecl* decl, JsonWriter& writer)
{
	writer.BeginArray();

	if (decl->isCompleteDefinition())
	{
		for (auto it = decl->bases_begin(); it != decl->bases_end(); ++it)
		{
			writer.String(TypeToString(it->getType()));
		}
	}

	writer.EndArray();
}

void BansheeDocGeneratorASTVisitor::WriteTemplateParams(const TemplateParameterList* templateParameterList, JsonWriter& writer)
{
	if (templateParameterList == nullptr)
	{
		writer.Null();
		return;
	}

	std::string buffer;
	raw_string_ostream stream(buffer);
	const PrintingPolicy policy = MakeDocPrintingPolicy(*mASTContext);
	templateParameterList->print(stream, *mASTContext, policy, /*OmitTemplateKW*/ true);
	stream.flush();

	writer.String(CollapseWhitespace(buffer));
}

std::string BansheeDocGeneratorASTVisitor::TypeToString(QualType type) const
{
	if (type.isNull())
		return std::string();

	const PrintingPolicy policy = MakeDocPrintingPolicy(*mASTContext);
	return type.getAsString(policy);
}

std::string BansheeDocGeneratorASTVisitor::PrettyPrintSignature(const Decl* declaration) const
{
	std::string buffer;
	raw_string_ostream stream(buffer);
	const PrintingPolicy policy = MakeDocPrintingPolicy(*mASTContext);
	declaration->print(stream, policy);
	stream.flush();

	return CollapseWhitespace(buffer);
}

std::string BansheeDocGeneratorASTVisitor::ExpressionToString(const Expr* expression) const
{
	if (expression == nullptr)
		return std::string();

	std::string buffer;
	raw_string_ostream stream(buffer);
	const PrintingPolicy policy = MakeDocPrintingPolicy(*mASTContext);
	expression->printPretty(stream, nullptr, policy);
	stream.flush();

	return CollapseWhitespace(buffer);
}

std::string BansheeDocGeneratorASTVisitor::FlattenComment(const SmallVectorImpl<CommentParagraph>& paragraphs) const
{
	std::string result;
	for (size_t i = 0; i < paragraphs.size(); ++i)
	{
		if (!result.empty())
			result += "\n\n";

		result += paragraphs[i].Text;
	}

	return result;
}

std::string BansheeDocGeneratorASTVisitor::DetectCopydocTarget(const CommentEntry& entry) const
{
	if (entry.Brief.empty())
		return std::string();

	const std::string& firstText = entry.Brief.front().Text;
	const std::string prefix = "@copydoc ";

	if (firstText.compare(0, prefix.size(), prefix) == 0)
		return TrimWhitespace(firstText.substr(prefix.size()));

	return std::string();
}

const char* BansheeDocGeneratorASTVisitor::VisibilityToString(AccessSpecifier accessSpecifier) const
{
	switch (accessSpecifier)
	{
	case AS_public:    return "public";
	case AS_protected: return "protected";
	case AS_private:   return "private";
	case AS_none:      return "public";
	}

	return "public";
}

bool BansheeDocGeneratorASTVisitor::IsInSystemHeader(const Decl* declaration) const
{
	const SourceManager& sourceManager = mASTContext->getSourceManager();
	return sourceManager.isInSystemHeader(declaration->getBeginLoc());
}
