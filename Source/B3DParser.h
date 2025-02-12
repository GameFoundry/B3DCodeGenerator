#pragma once

#include "B3DCommon.h"
#include "B3DCommentParser.h"

struct ScriptExportInformation;
struct FunctionTypeInfo;

class BansheeCodeGeneratorASTVisitor : public RecursiveASTVisitor<BansheeCodeGeneratorASTVisitor>
{
public:
	explicit BansheeCodeGeneratorASTVisitor(CompilerInstance* CI, CommentParser& commentParser);

	bool VisitEnumDecl(EnumDecl* decl);
	bool VisitCXXRecordDecl(CXXRecordDecl* declaration);

private:
	/**
	 * Attempts to evaluate a literal expression.
	 *
	 * @param	expression			Expression to evaluate. Supported are boolean, integer, float/double, nullptr and enum literal expressions.
	 * @param	evaluatedValue		Expression value as a string, if successful.
	 * @return						True if evaluation succeeded.
	 */
	bool TryEvaluateLiteral(Expr* expression, std::string& evaluatedValue);

	/**
	 * Attempts to evaluate an expression. This may be:
	 *  - Literal expressions that may be evaluated through TryEvaluateLiteral().
	 *  - Known default static values such as StringUtil::kBlank or UUID::kEmpty.
	 *  - Constructor call containing arguments that may be evaluated themselves via this method.
	 *  - Type casts
	 *
	 * @param expression				Expression to evaluate.
	 * @param outEvaluatedValue			Expression value as a string, if successful.
	 * @param outEvaluatedValueType		Type of the expression value. Will only be provided for expressions that require construction and cannot be passed as literals.
	 * @return							True if evaluation succeeded.
	 */
	bool TryEvaluateExpression(Expr* expression, std::string& outEvaluatedValue, std::string& outEvaluatedValueType);

	/**
	 * Attempts to parse the signature of an Event<> or std::function<>.
	 *
	 * @param type					Type to parse.
	 * @param outEventInformation	Parsed information, if successful.			
	 * @param outIsCallback			True if the parsed type is a std::function<> rather than an Event<>.
	 * @return						True if type is an Event<> or std::function<> and parsing was successful.
	 */
	bool TryParseEventSignature(QualType type, MethodInfo& outEventInformation, bool& outIsCallback);

	/**
	 * Attempts to parse a declaration of type Event<> or std::function<>.
	 *
	 * @param decl					Declaration to parse.
	 * @param className				Name on which the declaration is defined.
	 * @param outEventInformation	Parsed information, if successful.			
	 * @return						True if type is an Event<> or std::function<> and parsing was successful.
	 */
	bool TryParseEvent(ValueDecl* decl, const std::string& className, MethodInfo& outEventInformation);

	/**
	 * Attempts to parse type information.
	 *
	 * @param type			Type to parse.
	 * @param outType		Information about the type, if successful.
	 * @return				True if parsing was successful.
	 */
	bool ParseTypeInformation(QualType type, VariableTypeInformation& outType);

	/**
	 * Parses provided template arguments and returns a string containing the parsed argument values, as well as information about argument types.
	 *
	 * @param className							Name of the class we're parsing the template arguments for.
	 * @param arguments							Array of template arguments.
	 * @param argumentCount						Number of template arguments in @p arguments.
	 * @param outTemplateArgumentInformation	Output information about types of each template argument. This will be 'class' if the argument represents a type, or expression type if it represents an expression.
	 * @return									Template arguments surrounded by <>, separate by commas. e.g. <Vector3, 4>
	 */
	std::string ParseTemplateArguments(const std::string& className, const TemplateArgument* arguments, uint32_t argumentCount, std::vector<TemplateParameterInformation>* outTemplateArgumentInformation);

	/**
	 * Tries to parse the provided declaration as a struct. If declaration has already been parsed, returns false.
	 *
	 * @param	declaration					C++ class/struct declaration to parse.
	 * @param	scriptExportInformation		Information used for controlling the export, usually parsed from the export annotation attribute.
	 * @param	outStructInfo				Parsed struct information. Only valid if the method returns true.
	 * @return								True if the declaration was parsed. False if the declaration was already parsed previously.
	 */
	bool TryParseDeclarationAsStruct(CXXRecordDecl* declaration, const ScriptExportInformation& scriptExportInformation, StructInfo& outStructInfo);

	/**
	 * Tries to parse the provided declaration as a class. If declaration has already been parsed, returns false.
	 *
	 * @param	declaration					C++ class/struct declaration to parse.
	 * @param	scriptExportInformation		Information used for controlling the export, usually parsed from the export annotation attribute.
	 * @param	outClassInfo				Parsed class information. Only valid if the method returns true.
	 * @return								True if the declaration was parsed. False if the declaration was already parsed previously.
	 */
	bool TryParseDeclarationAsClass(CXXRecordDecl* declaration, const ScriptExportInformation& scriptExportInformation, ClassInfo& outClassInfo);

	ASTContext* astContext;
	Preprocessor& preprocessor;
	CommentParser& mCommentParser;
};
