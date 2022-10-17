#include "B3DParser.h"
#include "B3DParserUtility.h"
#include "B3DTypeLookup.h"

/** Parses the declaration and determines what exported type category should be used to represent this type in scripting. */
static ExportedClassTypeCategory DetermineExportedTypeCategory(const CXXRecordDecl* decl)
{
	std::stack<const CXXRecordDecl*> todo;
	todo.push(decl);

	while (!todo.empty())
	{
		const CXXRecordDecl* curDecl = todo.top();
		todo.pop();

		if (curDecl->hasDefinition())
		{
			auto iter = curDecl->bases_begin();
			while (iter != curDecl->bases_end())
			{
				const CXXBaseSpecifier* baseSpec = iter;
				CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

				std::string className = baseDecl->getName().str();

				if (className == kBuiltinComponentType)
					return ::ExportedClassTypeCategory::Component;
				else if (className == kBuiltinResourceType)
					return ::ExportedClassTypeCategory::Resource;
				else if (className == kBuiltinSceneObjectType)
					return ::ExportedClassTypeCategory::SceneObject;
				else if (className == kBuiltinGUIElementType)
					return ::ExportedClassTypeCategory::GUIElement;
				else if (className == kBuiltinReflectableType)
					return ::ExportedClassTypeCategory::ReflectableClass;

				todo.push(baseDecl);
				iter++;
			}
		}
	}

	return ::ExportedClassTypeCategory::Class;
}

/** Maps a builtin Clang type into a type in C#. */
static bool MapBuiltinTypeToCSharpType(BuiltinType::Kind kind, std::string& output)
{
	switch (kind)
	{
	case BuiltinType::Void:
		output = "void";
		return true;
	case BuiltinType::Bool:
		output = "bool";
		return true;
	case BuiltinType::Char_S:
		output = "byte";
		return true;
	case BuiltinType::Char_U:
		output = "byte";
		return true;
	case BuiltinType::SChar:
		output = "byte";
		return true;
	case BuiltinType::Short:
		output = "short";
		return true;
	case BuiltinType::Int:
		output = "int";
		return true;
	case BuiltinType::Long:
		output = "long";
		return true;
	case BuiltinType::LongLong:
		output = "long";
		return true;
	case BuiltinType::UChar:
		output = "byte";
		return true;
	case BuiltinType::UShort:
		output = "short";
		return true;
	case BuiltinType::UInt:
		output = "int";
		return true;
	case BuiltinType::ULong:
		output = "long";
		return true;
	case BuiltinType::ULongLong:
		output = "long";
		return true;
	case BuiltinType::Float:
		output = "float";
		return true;
	case BuiltinType::Double:
		output = "double";
		return true;
	case BuiltinType::WChar_S:
	case BuiltinType::WChar_U:
		output = "short";
		return true;
	case BuiltinType::Char16:
		output = "short";
		return true;
	case BuiltinType::Char32:
		output = "int";
		return true;
	default:
		break;
	}

	errs() << "Unrecognized builtin type found.\n";
	return false;
}

/** Returns the namespace the provided declaration is in. */
static std::string ParseNamespace(const RecordDecl* decl)
{
	std::string nsName;
	const DeclContext* nsContext = decl->getEnclosingNamespaceContext();
	if (nsContext != nullptr && nsContext->isNamespace())
	{
		// Note: Not checking more than one level of namespaces
		const NamespaceDecl* nsDecl = cast<NamespaceDecl>(nsContext);

		nsName = nsDecl->getName().str();
	}

	return nsName;
}

/** Parses script export attributes set of a field or a parameter. Appends the parsed information (if any) to the provided type information structure. */
bool ParseParameterOrFieldAttribute(Decl* decl, bool isField, VariableTypeInformation& typeInformation)
{
	for(const auto& entry : decl->specific_attrs<AnnotateAttr>())
	{
		if (!isField && entry->getAnnotation() == "params")
		{
			typeInformation.UnsetParameterFlag(ParameterFlags::VarParams, true);
			return true;
		}

		if (entry->getAnnotation() == "norref")
		{
			typeInformation.UnsetParameterFlag(ParameterFlags::AsResourceRef, true);
			return true;
		}
	}

	return false;
}

void parseNamespace(NamedDecl* decl, SmallVector<std::string, 4>& output)
{
	const DeclContext* context = decl->getDeclContext();
	SmallVector<const DeclContext *, 8> contexts;

	// Collect contexts.
	while (context && isa<NamedDecl>(context))
	{
		contexts.push_back(context);
		context = context->getParent();
	}

	for (const DeclContext* dc : reverse(contexts))
	{
		if (const auto* nd = dyn_cast<NamespaceDecl>(dc))
		{
			if (!nd->isAnonymousNamespace())
				output.push_back(nd->getDeclName().getAsString());
		}
	}
}

struct FunctionTypeInfo
{
	// Only relevant for function types
	std::vector<VariableBase> paramTypes;
	VariableBase returnType;
};

bool BansheeCodeGeneratorASTVisitor::ParseTypeInformation(QualType type, VariableTypeInformation& outType)
{
	// Note: Not supporting pointer to pointer or reference to pointer
	QualType realType;
	if (type->isPointerType())
	{
		realType = type->getPointeeType();
		outType.QualifierFlags |= (uint32_t)VariableQualifierFlags::IsPointer;
	}
	else if (type->isReferenceType())
	{
		realType = type->getPointeeType();
		outType.QualifierFlags |= (uint32_t)VariableQualifierFlags::IsReference;
	}
	else
		realType = type;

	// Note: Not checking const pointer
	if (realType.isConstQualified())
		outType.QualifierFlags |= (uint32_t)VariableQualifierFlags::IsConst;

	// Check for arrays & core variant types
	if (realType->isStructureOrClassType())
	{
		const TemplateSpecializationType* specType = realType->getAs<TemplateSpecializationType>();

		int numArgs = 0;

		if (specType != nullptr)
			numArgs = specType->getNumArgs();

		if (numArgs > 0)
		{
			const RecordType* recordType = realType->getAs<RecordType>();
			const RecordDecl* recordDecl = recordType->getDecl();

			std::string sourceTypeName = recordDecl->getName().str();

			// Note: vector parsing code copied below
			if (sourceTypeName == "vector" && recordDecl->isInStdNamespace())
			{
				outType.TypeName = "Vector";
				outType.TypeCategory = VariableTypeCategory::Vector;

				QualType underlyingType = specType->getArg(0).getAsType();
				VariableTypeInformation underlyingTypeInformation;
				if (!ParseTypeInformation(underlyingType, underlyingTypeInformation))
				{
					outs() << "Error: Failed parsing underlying Vector<T> type.\n";
					return false;
				}

				outType.UnderlyingType = std::make_unique<VariableTypeInformation>(std::move(underlyingTypeInformation));
				return true;
			}
			else if(sourceTypeName == "SmallVector")
			{
				outType.TypeName = "SmallVector";
				outType.TypeCategory = VariableTypeCategory::SmallVector;

				uint32_t smallVectorSize = 0;
				if(numArgs > 1)
				{
					std::string tmplArgExprValue, exprType;
					if (evaluateExpression(specType->getArg(1).getAsExpr(), tmplArgExprValue, exprType))
					{
						try
						{
							smallVectorSize = std::stoi(tmplArgExprValue);
						}
						catch(const std::invalid_argument& ex)
						{
							errs() << "Error: Cannot convert SmallVector size template argument to a number, ignoring it.\n";
						}
						catch(const std::out_of_range& ex)
						{
							errs() << "Error: Cannot convert SmallVector size template argument to a number, ignoring it.\n";
						}
						
					}
					else
						errs() << "Error: Template argument for SmallVector cannot be constantly evaluated, ignoring it.\n";
				}

				outType.ArraySize = smallVectorSize;

				QualType underlyingType = specType->getArg(0).getAsType();
				VariableTypeInformation underlyingTypeInformation;
				if (!ParseTypeInformation(underlyingType, underlyingTypeInformation))
				{
					errs() << "Error: Failed parsing underlying SmallVector<T> type.\n";
					return false;
				}

				outType.UnderlyingType = std::make_unique<VariableTypeInformation>(std::move(underlyingTypeInformation));
				return true;
			}
			else if(sourceTypeName == "ComponentOrActor")
			{
				outType.TypeName = "ComponentOrActor";
				outType.TypeCategory = VariableTypeCategory::ComponentOrActor;

				QualType underlyingType;
				bool foundUnderlying = false;
				const DeclContext* context = dyn_cast<DeclContext>(recordDecl);
				for (auto I = context->decls_begin(); I != context->decls_end(); ++I)
				{
					if (TypeAliasDecl* typeAliasDecl = dyn_cast<TypeAliasDecl>(*I))
					{
						if(typeAliasDecl->getName() == "HandleType")
						{
							underlyingType = typeAliasDecl->getUnderlyingType();
							foundUnderlying = true;
							break;
						}
					}
				}

				if(!foundUnderlying)
				{
					errs() << "Error: Cannot find underlying component type for ComponentOrActor<T>.\n";
					return false;
				}

				VariableTypeInformation underlyingTypeInformation;
				if (!ParseTypeInformation(underlyingType, underlyingTypeInformation))
				{
					errs() << "Error: Failed parsing underlying ComponentOrActor<T> type.\n";
					return false;
				}

				outType.UnderlyingType = std::make_unique<VariableTypeInformation>(std::move(underlyingTypeInformation));
				return true;
			}
			else if(sourceTypeName == "TAsyncOp")
			{
				outType.TypeName = "TAsyncOp";
				outType.TypeCategory = VariableTypeCategory::AsyncOp;

				QualType underlyingType = specType->getArg(0).getAsType();
				VariableTypeInformation underlyingTypeInformation;
				if (!ParseTypeInformation(underlyingType, underlyingTypeInformation))
				{
					errs() << "Error: Failed parsing underlying TAsyncOp<T> type.\n";
					return false;
				}

				outType.UnderlyingType = std::make_unique<VariableTypeInformation>(std::move(underlyingTypeInformation));
				return true;
			}
			else if(sourceTypeName == "Flags")
			{
				outType.TypeName = "Flags";
				outType.TypeCategory = VariableTypeCategory::Flags;

				if(numArgs > 1)
				{
					QualType storageType = specType->getArg(1).getAsType();
					bool validStorageType = false;
					if (storageType->isBuiltinType())
					{
						const BuiltinType* builtinType = realType->getAs<BuiltinType>();
						std::string storageTypeStr;
						if (ParserUtility::MapBuiltinPrimitiveTypeToCppType(builtinType->getKind(), storageTypeStr))
						{
							if (storageTypeStr == "uint32_t")
								validStorageType = true;
						}

						if(!validStorageType)
						{
							errs() << "Error: Invalid storage type used for Flags.\n";
							return false;
						}
					}
				}

				QualType underlyingType = specType->getArg(0).getAsType();
				VariableTypeInformation underlyingTypeInformation;
				if (!ParseTypeInformation(underlyingType, underlyingTypeInformation))
				{
					errs() << "Error: Failed parsing underlying Flags<T> type.\n";
					return false;
				}

				outType.UnderlyingType = std::make_unique<VariableTypeInformation>(std::move(underlyingTypeInformation));
				return true;
			}
			else if (sourceTypeName == "basic_string" && recordDecl->isInStdNamespace())
			{
				realType = specType->getArg(0).getAsType();

				const BuiltinType* builtinType = realType->getAs<BuiltinType>();
				if (builtinType->getKind() == BuiltinType::Kind::WChar_U ||
					builtinType->getKind() == BuiltinType::Kind::WChar_S)
				{
					outType.TypeName = "WString";
					outType.TypeCategory = VariableTypeCategory::WString;
				}
				else
				{
					outType.TypeName = "String";
					outType.TypeCategory = VariableTypeCategory::String;
				}

				return true;
			}
			else if (sourceTypeName == "shared_ptr" && recordDecl->isInStdNamespace())
			{
				outType.TypeName = "Shared";
				outType.TypeCategory = VariableTypeCategory::SharedPointer;

				QualType underlyingType = specType->getArg(0).getAsType();
				VariableTypeInformation underlyingTypeInformation;
				if (!ParseTypeInformation(underlyingType, underlyingTypeInformation))
				{
					errs() << "Error: Failed parsing underlying Shared<T> type.\n";
					return false;
				}

				outType.UnderlyingType = std::make_unique<VariableTypeInformation>(std::move(underlyingTypeInformation));
				if (outType.UnderlyingType->TypeCategory == VariableTypeCategory::GameObjectHandle || outType.UnderlyingType->TypeCategory == VariableTypeCategory::ResourceHandle)
				{
					errs() << "Error: Game object and resource types are only allowed to be referenced through handles for scripting purposes\n";
					return false;
				}

				return true;
			}
			else if (sourceTypeName == "TResourceHandle")
			{
				// Note: Not supporting weak resource handles

				outType.TypeName = "TResourceHandle";
				outType.TypeCategory = VariableTypeCategory::ResourceHandle;
				outType.ParameterFlags |= (uint32_t)ParameterFlags::AsResourceRef; // Set this here, as we want to make it a default

				QualType underlyingType = specType->getArg(0).getAsType();
				VariableTypeInformation underlyingTypeInformation;
				if (!ParseTypeInformation(underlyingType, underlyingTypeInformation))
				{
					errs() << "Error: Failed parsing underlying TResourceHandle<T> type.\n";
					return false;
				}

				outType.UnderlyingType = std::make_unique<VariableTypeInformation>(std::move(underlyingTypeInformation));
				return true;
			}
			else if (sourceTypeName == "GameObjectHandle")
			{
				realType = specType->getArg(0).getAsType();
				outType.TypeName = "GameObjectHandle";
				outType.TypeCategory = VariableTypeCategory::GameObjectHandle;

				QualType underlyingType = specType->getArg(0).getAsType();
				VariableTypeInformation underlyingTypeInformation;
				if (!ParseTypeInformation(underlyingType, underlyingTypeInformation))
				{
					errs() << "Error: Failed parsing underlying TResourceHandle<T> type.\n";
					return false;
				}

				outType.UnderlyingType = std::make_unique<VariableTypeInformation>(std::move(underlyingTypeInformation));
				return true;
			}
			else
			{
				const TemplateDecl* templateDecl = specType->getTemplateName().getAsTemplateDecl();
				if(templateDecl)
				{
					std::string templateDeclName = templateDecl->getName().str();

					// Core variant types can be accessed transparently, so we just reference the underlying type directly
					if ((templateDeclName == "CoreVariantType" || templateDeclName == "CoreVariantHandleType") && specType->isTypeAlias())
					{
						realType = specType->getAliasedType();
						return ParseTypeInformation(realType, outType);
					}
				}
			}
		}
	}
	else if(realType->isArrayType())
	{
		const ConstantArrayType* arrayType = dyn_cast<ConstantArrayType>(astContext->getAsArrayType(realType));
		if (arrayType)
		{
			outType.ArraySize = (unsigned)arrayType->getSize().getZExtValue();
			outType.TypeCategory = VariableTypeCategory::Array;

			QualType underlyingType = arrayType->getElementType();
			VariableTypeInformation underlyingTypeInformation;
			if (!ParseTypeInformation(underlyingType, underlyingTypeInformation))
			{
				errs() << "Error: Failed parsing underlying Array<T> type.\n";
				return false;
			}

			outType.UnderlyingType = std::make_unique<VariableTypeInformation>(std::move(underlyingTypeInformation));
			return true;
		}
	}

	if (realType->isPointerType())
	{
		errs() << "Error: Only normal pointers are supported for parameter types.\n";
		return false;
	}

	if (realType->isBuiltinType())
	{
		const BuiltinType* builtinType = realType->getAs<BuiltinType>();
		if (!ParserUtility::MapBuiltinPrimitiveTypeToCppType(builtinType->getKind(), outType.TypeName))
			return false;

		outType.TypeCategory = VariableTypeCategory::Primitive;
		return true;
	}
	else if (realType->isStructureOrClassType())
	{
		const RecordType* recordType = realType->getAs<RecordType>();
		const RecordDecl* recordDecl = recordType->getDecl();

		std::string sourceTypeName = recordDecl->getName().str();

		// Handle special templated types
		const TemplateSpecializationType* specType = realType->getAs<TemplateSpecializationType>();
		if (specType != nullptr)
		{
			int numArgs = specType->getNumArgs();
			if (numArgs > 0)
			{
				// Handle generic template specializations
				sourceTypeName += parseTemplArguments(sourceTypeName, specType->getArgs(), specType->getNumArgs(), nullptr);
			}
		}
		else
		{
			// Check for a direct pointer to a managed object
			if(sourceTypeName == "_MonoObject")
			{
				if (!outType.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
				{
					errs() << "Error: Found an object of type MonoObject but not passed by pointer. This is not supported. \n";
					return false;
				}

				outType.TypeName = "_MonoObject";
				outType.TypeCategory = VariableTypeCategory::MonoObject;

				return true;
			}
			else if(sourceTypeName == "Path")
			{
				outType.TypeName = "Path";
				outType.TypeCategory = VariableTypeCategory::Path;

				return true;
			}
			else if (sourceTypeName == "StringID")
			{
				outType.TypeName = "StringID";
				outType.TypeCategory = VariableTypeCategory::String;

				return true;
			}
		}

		// Its a user-defined type
		outType.TypeName = sourceTypeName;
		outType.TypeCategory = VariableTypeCategory::General;

		return true;
	}
	else if (realType->isEnumeralType())
	{
		const EnumType* enumType = realType->getAs<EnumType>();
		const EnumDecl* enumDecl = enumType->getDecl();

		std::string sourceTypeName = enumDecl->getName().str();
		outType.TypeName = sourceTypeName;
		outType.TypeCategory = VariableTypeCategory::General;

		return true;
	}
	else
	{
		errs() << "Error: Unrecognized type\n";
		return false;
	}
}

bool BansheeCodeGeneratorASTVisitor::parseEventSignature(QualType type, FunctionTypeInfo& typeInfo, bool& isCallback)
{
	if (type->isStructureOrClassType())
	{
		const TemplateSpecializationType* specType = type->getAs<TemplateSpecializationType>();
		int numArgs = 0;

		if (specType != nullptr)
			numArgs = specType->getNumArgs();

		if (numArgs > 0)
		{
			const RecordType* recordType = type->getAs<RecordType>();
			const RecordDecl* recordDecl = recordType->getDecl();

			std::string sourceTypeName = recordDecl->getName().str();
			std::string nsName = ParseNamespace(recordDecl);

			bool isEvent = false;
			if (sourceTypeName == "Event" && nsName == sFrameworkCppNs)
				isEvent = true;
			else if(sourceTypeName == "function" && recordDecl->isInStdNamespace())
			{
				isEvent = true;
				isCallback = true;
			}

			if (isEvent)
			{
				type = specType->getArg(0).getAsType();
				if(type->isFunctionProtoType())
				{
					const FunctionProtoType* funcType = type->getAs<FunctionProtoType>();

					unsigned int numParams = funcType->getNumParams();
					typeInfo.paramTypes.resize(numParams);

					for(unsigned int i = 0; i < numParams; i++)
					{
						QualType paramType = funcType->getParamType(i);
						ParseTypeInformation(paramType, typeInfo.paramTypes[i].TypeInformation);
					}

					QualType returnType = funcType->getReturnType();
					if (!returnType->isVoidType())
						ParseTypeInformation(returnType, typeInfo.returnType.TypeInformation);
				}

				return true;
			}
		}
	}

	return false;
}

BansheeCodeGeneratorASTVisitor::BansheeCodeGeneratorASTVisitor(CompilerInstance* CI, CommentParser& commentParser)
	:astContext(&(CI->getASTContext())), preprocessor(CI->getPreprocessor()), mCommentParser(commentParser)
{ }

bool BansheeCodeGeneratorASTVisitor::evaluateLiteral(Expr* expr, std::string& evalValue)
{
	QualType type = expr->getType();
	if (type->isBuiltinType())
	{
		const BuiltinType* builtinType = type->getAs<BuiltinType>();
		switch (builtinType->getKind())
		{
		case BuiltinType::Bool:
		{
			bool result;
			expr->EvaluateAsBooleanCondition(result, *astContext);

			evalValue = result ? "true" : "false";

			return true;
		}
		case BuiltinType::Char_S:
		case BuiltinType::Char_U:
		case BuiltinType::SChar:
		case BuiltinType::Short:
		case BuiltinType::Int:
		case BuiltinType::Long:
		case BuiltinType::LongLong:
		case BuiltinType::UChar:
		case BuiltinType::UShort:
		case BuiltinType::UInt:
		case BuiltinType::ULong:
		case BuiltinType::ULongLong:
		case BuiltinType::WChar_S:
		case BuiltinType::WChar_U:
		case BuiltinType::Char16:
		case BuiltinType::Char32:
		{
			Expr::EvalResult result;
			expr->EvaluateAsInt(result, *astContext);

			SmallString<5> valueStr;

			result.Val.getInt().toString(valueStr);
			evalValue = valueStr.str().str();

			return true;
		}
		case BuiltinType::Float:
		{
			APFloat result(0.0f);
			expr->EvaluateAsFloat(result, *astContext);

			SmallString<8> valueStr;
			result.toString(valueStr);
			evalValue = valueStr.str().str() + "f";

			return true;
		}
		case BuiltinType::Double:
		{
			APFloat result(0.0f);
			expr->EvaluateAsFloat(result, *astContext);

			SmallString<8> valueStr;
			result.toString(valueStr);
			evalValue = valueStr.str().str();

			return true;
		}
		case BuiltinType::NullPtr:
		{
			evalValue = "null";
			return true;
		}
		default:
			return false;
		}
	}
	else if(type->isEnumeralType())
	{
		const EnumType* enumType = type->getAs<EnumType>();
		const EnumDecl* enumDecl = enumType->getDecl();

		Expr::EvalResult result;
		expr->EvaluateAsInt(result, *astContext);

		SmallString<5> valueStr;
		result.Val.getInt().toString(valueStr);
		evalValue = valueStr.str().str();

		return true;
	}

	return false;
}

bool BansheeCodeGeneratorASTVisitor::evaluateExpression(Expr* expr, std::string& evalValue, std::string& valType)
{
	if (expr->isEvaluatable(*astContext))
	{
		if (evaluateLiteral(expr, evalValue))
			return true;
	}

	// Check for nullptr, literals in constructors and cast literals
	if (ExprWithCleanups* cleanups = dyn_cast<ExprWithCleanups>(expr))
		expr = cleanups->getSubExpr();

	// Skip through reference binding to temporary.
	if (MaterializeTemporaryExpr* materialize = dyn_cast<MaterializeTemporaryExpr>(expr))
		expr = materialize->getSubExpr();

	// Skip any temporary bindings; they're implicit.
	if (CXXBindTemporaryExpr* binder = dyn_cast<CXXBindTemporaryExpr>(expr))
		expr = binder->getSubExpr();

	expr = expr->IgnoreParenCasts();

	// Reference to some other declaration (e.g. a static)
	DeclRefExpr* declRefExpr = dyn_cast<DeclRefExpr>(expr);
	if(declRefExpr)
	{
		ValueDecl* decl = declRefExpr->getDecl();
		const std::string name = ParserUtility::GetFullName(decl);

		if(name == (sFrameworkCppNs + "::StringUtil::BLANK") || name == (sFrameworkCppNs + "::StringUtil::WBLANK"))
		{
			evalValue = "\"\"";
			valType = "";
			return true;
		}
		else if(name == (sFrameworkCppNs + "::UUID::EMPTY"))
		{
			evalValue = "";
			valType = "UUID";
			return true;
		}
	}

	CXXConstructExpr* ctorExp = dyn_cast<CXXConstructExpr>(expr);
	if (!ctorExp)
		return false;

	// Check for special case of a single null parameter
	{
		expr = ctorExp->getArg(0);

		bool isNull = false;
		QualType type = expr->getType();
		if (type->isBuiltinType())
		{
			const BuiltinType* builtinType = type->getAs<BuiltinType>();
			if (builtinType->getKind() == BuiltinType::NullPtr)
			{
				evalValue = "null";
				return true;
			}
		}
	}

	// Constructor or cast of some type
	QualType parentType = ctorExp->getType();

	VariableBase variableInformation;
	ParseTypeInformation(parentType, variableInformation.TypeInformation);
	valType = variableInformation.TypeInformation.GetLastWrappedOrSelfTypeName();

	for(int i = 0; i < ctorExp->getNumArgs(); i++)
	{
		if (i != 0)
			evalValue += ", ";

		std::string argValue;
		expr = ctorExp->getArg(i);

		bool isNull = false;
		QualType type = expr->getType();
		if(type->isBuiltinType())
		{
			const BuiltinType* builtinType = type->getAs<BuiltinType>();
			if (builtinType->getKind() == BuiltinType::NullPtr)
			{
				argValue = "null";
				isNull = true;
			}
		}

		if(!isNull)
		{
			if (expr->isEvaluatable(*astContext))
			{
				if (!evaluateLiteral(expr, argValue))
					return false;
			}
			else
			{
				std::string dummy3;
				if (!evaluateExpression(expr, argValue, dummy3))
					return false;
			}
		}
			
		evalValue += argValue;
	}

	return true;
}

bool BansheeCodeGeneratorASTVisitor::parseEvent(ValueDecl* decl, const std::string& className, MethodInfo& eventInfo)
{
	AnnotateAttr* fieldAttr = decl->getAttr<AnnotateAttr>();
	if (fieldAttr == nullptr)
		return false;

	StringRef sourceFieldName = decl->getName();

	ScriptExportInformation parsedEventInfo;
	if (!ScriptExportUtility::ParseExportAttribute(fieldAttr, sourceFieldName, parsedEventInfo))
		return false;

	FunctionTypeInfo eventSignature;
	bool isCallback = false;
	if (!parseEventSignature(decl->getType(), eventSignature, isCallback))
		return false;

	if (decl->getAccess() != AS_public)
		outs() << "Error: Exported event \"" + sourceFieldName + "\" isn't public. This will likely result in invalid code generation.";

	int eventFlags = 0;

	if ((parsedEventInfo.ExportFlags & (int)ExportFlags::ExternalMethod) != 0)
	{
		outs() << "Error: External events currently not supported. Skipping export for event \"" + sourceFieldName + "\".";
		return false;
	}

	if ((parsedEventInfo.ExportFlags & (int)ExportFlags::InteropOnly))
		eventFlags |= (int)MethodFlags::InteropOnly;

	if (isCallback)
		eventFlags |= (int)MethodFlags::Callback;

	eventInfo.NativeName = sourceFieldName.str();
	eventInfo.ScriptName = parsedEventInfo.ExportedTypeName;
	eventInfo.MethodFlags = eventFlags;
	eventInfo.ExternalClass = className;
	eventInfo.Visibility = parsedEventInfo.Visibility;
	eventInfo.API = ParserUtility::ParseAPIFromExportFlags(parsedEventInfo.ExportFlags);
	mCommentParser.ParseComments(decl, eventInfo.Documentation);
	CommentParser::ClearParameterReferenceComments(eventInfo.Documentation);

	if (!eventSignature.returnType.TypeInformation.IsEmpty())
	{
		eventInfo.ReturnValue.TypeInformation = eventSignature.returnType.TypeInformation;
	}

	int idx = 0;
	for(auto& entry : eventSignature.paramTypes)
	{
		VariableInformation paramInfo;
		paramInfo.Name = "p" + std::to_string(idx);
		paramInfo.TypeInformation = entry.TypeInformation;

		eventInfo.Parameters.push_back(paramInfo);
		idx++;
	}

	return true;
}

bool BansheeCodeGeneratorASTVisitor::VisitEnumDecl(EnumDecl* decl)
{
	mCommentParser.ParseAndRegisterAllComments(decl);

	AnnotateAttr* attr = decl->getAttr<AnnotateAttr>();
	if (attr == nullptr)
		return true;

	StringRef sourceClassName = decl->getName();
	ScriptExportInformation parsedEnumInfo;
	parsedEnumInfo.ExportedTypeName = sourceClassName.str();

	if (!ScriptExportUtility::ParseExportAttribute(attr, sourceClassName, parsedEnumInfo))
		return true;

	if (TypeLookup::FindEnumInformationInFile(parsedEnumInfo.ExportedFileName, sourceClassName.str()) != nullptr)
		return true; // Already parsed

	QualType underlyingType = decl->getIntegerType();
	if (!underlyingType->isBuiltinType())
	{
		outs() << "Error: Found an enum with non-builtin underlying type, skipping.\n";
		return true;
	}

	EnumInfo enumEntry;
	enumEntry.NativeName = sourceClassName.str();
	enumEntry.ScriptName = parsedEnumInfo.ExportedTypeName;
	enumEntry.Visibility = parsedEnumInfo.Visibility;
	enumEntry.API = ParserUtility::ParseAPIFromExportFlags(parsedEnumInfo.ExportFlags);
	enumEntry.DocumentationGroup = parsedEnumInfo.DocumentationGroup;
	mCommentParser.ParseComments(decl, enumEntry.Documentation);
	CommentParser::ClearParameterReferenceComments(enumEntry.Documentation);

	parseNamespace(decl, enumEntry.Namespace);

	const BuiltinType* builtinType = underlyingType->getAs<BuiltinType>();

	std::string enumType;
	if (builtinType->getKind() != BuiltinType::Kind::Int)
		MapBuiltinTypeToCSharpType(builtinType->getKind(), enumEntry.ExplicitUnderlyingCSharpType);

	std::string declFile = astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()).str();
	std::string destFile = "BsScript" + parsedEnumInfo.ExportedFileName + ".generated.h";
	std::string destFileEditor = "BsScript" + parsedEnumInfo.ExportedFileName + ".editor.generated.h";

	TypeLookup::RegisterNativeToScriptTypeMapping(enumEntry.Namespace, sourceClassName.str(), declFile, parsedEnumInfo.ExportedTypeName, parsedEnumInfo.ExportedFileName, enumEntry.API, ExportedClassTypeCategory::Enum, builtinType->getKind());

	auto iter = decl->enumerator_begin();
	while (iter != decl->enumerator_end())
	{
		EnumConstantDecl* constDecl = *iter;

		ScriptExportInformation parsedEnumEntryInfo;
		AnnotateAttr* enumAttr = constDecl->getAttr<AnnotateAttr>();

		StringRef entryName = constDecl->getName();
		parsedEnumEntryInfo.ExportedTypeName = entryName.str();
		parsedEnumEntryInfo.ExportFlags = 0;

		if (enumAttr != nullptr)
			ScriptExportUtility::ParseExportAttribute(enumAttr, entryName, parsedEnumEntryInfo);

		if ((parsedEnumEntryInfo.ExportFlags & (int)ExportFlags::Exclude) != 0)
		{
			++iter;
			continue;
		}

		const APSInt& entryVal = constDecl->getInitVal();

		EnumEntryInfo entryInfo;
		entryInfo.NativeName = entryName.str();
		entryInfo.ScriptName = parsedEnumEntryInfo.ExportedTypeName;
		mCommentParser.ParseComments(constDecl, entryInfo.Documentation);
		CommentParser::ClearParameterReferenceComments(entryInfo.Documentation);

		SmallString<5> valueStr;
		entryVal.toString(valueStr);
		entryInfo.Value = valueStr.str().str();

		enumEntry.Entries[(int)entryVal.getExtValue()] = entryInfo;
		++iter;
	}

	TypeLookup::RegisterEntryToGenerate(parsedEnumInfo.ExportedFileName, enumEntry);
	return true;
}

std::string BansheeCodeGeneratorASTVisitor::parseTemplArguments(const std::string& className, const TemplateArgument* tmplArgs, unsigned numArgs, SmallVector<TemplateParamInfo, 0>* templParams)
{
	std::stringstream tmplArgsStream;
	tmplArgsStream << "<";
	for(unsigned i = 0; i < numArgs; i++)
	{
		if (i != 0)
			tmplArgsStream << ", ";

		auto& tmplArg = tmplArgs[i];
		if(tmplArg.getKind() == TemplateArgument::Type)
		{
			VariableBase variableInformation;
			ParseTypeInformation(tmplArg.getAsType(), variableInformation.TypeInformation);

			tmplArgsStream << variableInformation.TypeInformation.GetLastWrappedOrSelfTypeName();

			if(templParams != nullptr)
				templParams->push_back({ "class" });
		}
		else if(tmplArg.getKind() == TemplateArgument::Expression)
		{
			std::string tmplArgExprValue, exprType;
			if (!evaluateExpression(tmplArg.getAsExpr(), tmplArgExprValue, exprType))
			{
				outs() << "Error: Template argument for type \"" << className << "\" cannot be constantly evaluated, ignoring it.\n";
				tmplArgsStream << "unknown";
			}
			else
				tmplArgsStream << tmplArgExprValue;

			VariableBase variableInformation;
			ParseTypeInformation(tmplArg.getAsExpr()->getType(), variableInformation.TypeInformation);

			if(templParams != nullptr)
				templParams->push_back({ variableInformation.TypeInformation.GetLastWrappedOrSelfTypeName() });
		}
		else
		{
			outs() << "Error: Cannot parse template argument for type: \"" << className << "\". \n";
			tmplArgsStream << "unknown";

			if(templParams != nullptr)
				templParams->push_back({ "unknown" });
		}
	}

	tmplArgsStream << ">";
	return tmplArgsStream.str();
}

bool BansheeCodeGeneratorASTVisitor::VisitCXXRecordDecl(CXXRecordDecl* decl)
{
	mCommentParser.ParseAndRegisterAllComments(decl);

	AnnotateAttr* attr = decl->getAttr<AnnotateAttr>();
	if (attr == nullptr)
		return true;

	StringRef declName = decl->getName();

	ScriptExportInformation parsedClassInfo;
	parsedClassInfo.ExportedTypeName = declName.str();

	if (!ScriptExportUtility::ParseExportAttribute(attr, declName, parsedClassInfo))
		return true;

	std::string srcClassName = declName.str();

	// If a template specialization append template params to its name
	ClassTemplateSpecializationDecl* specDecl = dyn_cast<ClassTemplateSpecializationDecl>(decl);
	CXXRecordDecl* templatedDecl = decl;
	SmallVector<TemplateParamInfo, 0> templParams;
	if(specDecl != nullptr)
	{
		auto& tmplArgs = specDecl->getTemplateInstantiationArgs();
		srcClassName += parseTemplArguments(srcClassName, tmplArgs.data(), tmplArgs.size(), &templParams);
		templatedDecl = specDecl->getSpecializedTemplate()->getTemplatedDecl();
	}

	if ((parsedClassInfo.ExportFlags & (int)ExportFlags::ExportAsStruct) != 0)
	{
		if (TypeLookup::FindStructInformationInFile(parsedClassInfo.ExportedFileName, srcClassName) != nullptr)
			return true; // Already parsed

		StructInfo structInfo;
		structInfo.NativeName = srcClassName;
		structInfo.NativeNameWithoutTemplateArguments = declName.str();
		structInfo.BaseClassName = ScriptExportUtility::FindExportableBasePlainClassName(decl);
		structInfo.Visibility = parsedClassInfo.Visibility;
		structInfo.RequiresInteropType = decl->isPolymorphic();
		structInfo.DocumentationGroup = parsedClassInfo.DocumentationGroup;
		structInfo.IsTemplateInstatiation = specDecl != nullptr;
		structInfo.TemplateParameters = templParams;
		structInfo.API = ParserUtility::ParseAPIFromExportFlags(parsedClassInfo.ExportFlags);

		mCommentParser.ParseComments(templatedDecl, structInfo.Documentation);
		parseNamespace(decl, structInfo.Namespace);
		CommentParser::ClearParameterReferenceComments(structInfo.Documentation);

		std::unordered_map<FieldDecl*, std::pair<std::string, std::string>> defaultFieldValues;

		// Parse non-default constructors & determine default values for fields
		if (decl->hasUserDeclaredConstructor())
		{
			auto ctorIter = decl->ctor_begin();
			while (ctorIter != decl->ctor_end())
			{
				SimpleConstructorInfo ctorInfo;
				CXXConstructorDecl* ctorDecl = *ctorIter;

				if (ctorDecl->isImplicit())
				{
					++ctorIter;
					continue;
				}

				AnnotateAttr* ctorAttr = ctorDecl->getAttr<AnnotateAttr>();
				if (ctorAttr != nullptr)
				{
					ScriptExportInformation parsedCtorInfo;
					ScriptExportUtility::ParseExportAttribute(ctorAttr, srcClassName, parsedCtorInfo);

					if ((parsedCtorInfo.ExportFlags & (int)ExportFlags::Exclude) != 0)
					{
						++ctorIter;
						continue;
					}
				}

				mCommentParser.ParseComments(ctorDecl, ctorInfo.Documentation);

				bool skippedDefaultArgument = false;
				for (auto I = ctorDecl->param_begin(); I != ctorDecl->param_end(); ++I)
				{
					ParmVarDecl* paramDecl = *I;

					VariableInformation paramInfo;
					paramInfo.Name = paramDecl->getName().str();

					std::string typeName;
					unsigned arraySize;
					if (!ParseTypeInformation(paramDecl->getType(), paramInfo.TypeInformation))
					{
						outs() << "Error: Unable to detect type for constructor parameter \"" << paramDecl->getName().str()
							<< "\". Skipping.\n";
						continue;
					}

					if (paramDecl->hasDefaultArg() && !skippedDefaultArgument)
					{
						if (!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.DefaultValue, paramInfo.DefaultValueType))
						{
							outs() << "Error: Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
								<< "argument that cannot be constantly evaluated, ignoring it.\n";
							skippedDefaultArgument = true;
						}
					}

					ctorInfo.Parameters.push_back(paramInfo);
				}

				std::unordered_map<FieldDecl*, ParmVarDecl*> assignments;

				// Parse initializers for assignments & default values
				for (auto I = ctorDecl->init_begin(); I != ctorDecl->init_end(); ++I)
				{
					CXXCtorInitializer* init = *I;

					if (init->isMemberInitializer())
					{
						FieldDecl* field = init->getMember();
						Expr* initExpr = init->getInit();

						bool isValid = true;
						while(CXXConstructExpr* constructExpr = dyn_cast<CXXConstructExpr>(initExpr))
						{
							isValid = false;
							if(constructExpr->getNumArgs() == 0)
							{
								// Don't care about default constructors
								break;
							}
							else if (constructExpr->getNumArgs() == 1)
							{
								initExpr = constructExpr->getArg(0);
								isValid = true;
							}
							else
							{
								outs() << "Error: Invalid number of parameters in constructor initializer. Only one parameter "
									"constructors are supported. In struct \"" + srcClassName + "\".\n";
								break;
							}
						}

						// Let the member initializer code handle the default value
						if (dyn_cast<CXXDefaultInitExpr>(initExpr))
							isValid = false;
						
						if (isValid)
						{
							// Check for constant value first
							std::string evalValue, evalTypeValue;
							if (evaluateExpression(initExpr, evalValue, evalTypeValue))
								defaultFieldValues[field] = std::make_pair(evalValue, evalTypeValue);
							else // Check for initializers referencing parameters
							{
								Decl* varDecl = nullptr;

								// Check for std::move
								if (CallExpr* callExpr = dyn_cast<CallExpr>(initExpr))
								{
									if(FunctionDecl* funcDecl = dyn_cast<FunctionDecl>(callExpr->getCalleeDecl()))
									{
										if(funcDecl->getName() == "move" && funcDecl->isInStdNamespace())
										{
											if(callExpr->getNumArgs() == 1)
											{
												if (Expr* argExpr = callExpr->getArg(0))
													varDecl = argExpr->getReferencedDeclOfCallee();
											}
										}
										
									}
								}
								else
								{
									varDecl = initExpr->getReferencedDeclOfCallee();
								}

								if (varDecl != nullptr)
								{
									ParmVarDecl* parmVarDecl = dyn_cast<ParmVarDecl>(varDecl);
									if (parmVarDecl != nullptr)
										assignments[field] = parmVarDecl;
								}
								else
								{
									std::string fieldName;

									if (field)
										fieldName = field->getName().str();

									outs() << "Error: Unrecognized initializer format in struct \"" << srcClassName << "\" for field \"" << fieldName << "\".\n";
								}
							}
						}
					}
				}

				// Parse any assignments in the function body
				// Note: Searching for trivially simple assignments only, ignoring anything else
				if (ctorDecl->hasBody())
				{
					CompoundStmt* functionBody = dyn_cast<CompoundStmt>(ctorDecl->getBody()); // Note: Not handling inner blocks
					assert(functionBody != nullptr);

					for (auto I = functionBody->child_begin(); I != functionBody->child_end(); ++I)
					{
						Stmt* stmt = *I;

						BinaryOperator* binaryOp = dyn_cast<BinaryOperator>(stmt);
						if (binaryOp == nullptr)
							continue;

						if (binaryOp->getOpcode() != BO_Assign)
							continue;

						Expr* lhsExpr = binaryOp->getLHS()->IgnoreParenCasts(); // Note: Ignoring even explicit casts
						Decl* lhsDecl;

						if (DeclRefExpr* varExpr = dyn_cast<DeclRefExpr>(lhsExpr))
							lhsDecl = varExpr->getDecl();
						else if (MemberExpr* memberExpr = dyn_cast<MemberExpr>(lhsExpr))
							lhsDecl = memberExpr->getMemberDecl();
						else
							continue;

						FieldDecl* fieldDecl = dyn_cast<FieldDecl>(lhsDecl);
						if (fieldDecl == nullptr)
							continue;

						Expr* rhsExpr = binaryOp->getRHS()->IgnoreParenCasts();
						Decl* rhsDecl = nullptr;

						if (DeclRefExpr* varExpr = dyn_cast<DeclRefExpr>(rhsExpr))
							rhsDecl = varExpr->getDecl();
						else if (MemberExpr* memberExpr = dyn_cast<MemberExpr>(rhsExpr))
							rhsDecl = memberExpr->getMemberDecl();

						ParmVarDecl* parmVarDecl = nullptr;
						if (rhsDecl != nullptr)
							parmVarDecl = dyn_cast<ParmVarDecl>(rhsDecl);

						if (parmVarDecl == nullptr)
						{
							outs() << "Warning: Found a non-trivial field assignment for field \"" << fieldDecl->getName() << "\" in"
								<< " constructor of \"" << srcClassName << "\". Ignoring assignment.\n";
							continue;
						}

						assignments[fieldDecl] = parmVarDecl;
					}
				}

				for (auto I = decl->field_begin(); I != decl->field_end(); ++I)
				{
					auto iterFind = assignments.find(*I);
					if (iterFind == assignments.end())
						continue;

					std::string fieldName = iterFind->first->getName().str();
					std::string paramName = iterFind->second->getName().str();

					ctorInfo.FieldAssignments[fieldName] = paramName;
				}

				CommentParser::EnsureValidParameterReferenceComments(ctorInfo.Parameters, ctorInfo.Documentation);

				structInfo.Constructors.push_back(ctorInfo);
				++ctorIter;
			}
		}

		std::stack<const CXXRecordDecl*> todo;
		todo.push(decl);

		bool hasDefaultValue = false;
		while (!todo.empty())
		{
			const CXXRecordDecl* curDecl = todo.top();
			todo.pop();

			for (auto I = curDecl->field_begin(); I != curDecl->field_end(); ++I)
			{
				FieldDecl* fieldDecl = *I;
				FieldInfo fieldInfo;
				fieldInfo.Name = fieldDecl->getName().str();

				ScriptExportInformation parsedFieldInfo;
				if (ScriptExportUtility::ParseExportAttribute(fieldDecl, srcClassName, parsedFieldInfo))
				{
					if ((parsedFieldInfo.ExportFlags & (int)ExportFlags::Exclude) != 0)
					{
						structInfo.RequiresInteropType = true;
						continue;
					}

					fieldInfo.Style = parsedFieldInfo.style;
				}

				auto iterFind = defaultFieldValues.find(fieldDecl);
				if (iterFind != defaultFieldValues.end())
				{
					fieldInfo.DefaultValue = iterFind->second.first;
					fieldInfo.DefaultValueType = iterFind->second.second;
				}

				if (fieldDecl->hasInClassInitializer())
				{
					Expr* initExpr = fieldDecl->getInClassInitializer();

					evaluateExpression(initExpr, fieldInfo.DefaultValue, fieldInfo.DefaultValueType);
				}

				std::string typeName;
				if (!ParseTypeInformation(fieldDecl->getType(), fieldInfo.TypeInformation))
				{
					outs() << "Error: Unable to detect type for field \"" << fieldDecl->getName().str() << "\" in \""
						<< srcClassName << "\". Skipping field.\n";
					continue;
				}

				ParseParameterOrFieldAttribute(fieldDecl, true, fieldInfo.TypeInformation);

				// Remove the pass-as-resource-ref flag to all parameters initializing the field
				if(!fieldInfo.TypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
				{
					for(auto& ctorInfo : structInfo.Constructors)
					{
						auto iterFindField = ctorInfo.FieldAssignments.find(fieldInfo.Name);
						if (iterFindField != ctorInfo.FieldAssignments.end())
						{
							auto iterFindParam = std::find_if(ctorInfo.Parameters.begin(), ctorInfo.Parameters.end(), 
								[name = iterFindField->second](const VariableInformation& varInfo)
							{
								return varInfo.Name == name;
							});

							if (iterFindParam != ctorInfo.Parameters.end())
							{
								iterFindParam->TypeInformation.UnsetParameterFlag(ParameterFlags::AsResourceRef, true);
							}
						}
					}
				}

				if (!fieldInfo.DefaultValue.empty())
					hasDefaultValue = true;

				mCommentParser.ParseComments(fieldDecl, fieldInfo.Documentation);
				CommentParser::ClearParameterReferenceComments(fieldInfo.Documentation);

				structInfo.Fields.push_back(fieldInfo);
			}

			auto iter = curDecl->bases_begin();
			while (iter != curDecl->bases_end())
			{
				const CXXBaseSpecifier* baseSpec = iter;
				CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

				todo.push(baseDecl);
				iter++;
			}
		}

		// If struct has in-class default values assigned, but no explicit constructors, add a parameterless constructor
		if (structInfo.Constructors.empty() && hasDefaultValue)
			structInfo.Constructors.push_back(SimpleConstructorInfo());

		std::string declFile = astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()).str();
		TypeLookup::RegisterNativeToScriptTypeMapping(structInfo.Namespace, srcClassName, declFile, parsedClassInfo.ExportedTypeName, parsedClassInfo.ExportedFileName, structInfo.API, ExportedClassTypeCategory::Struct);
		TypeLookup::RegisterEntryToGenerate(parsedClassInfo.ExportedFileName, structInfo);
	}
	else
	{
		if (TypeLookup::FindClassInformationInFile(parsedClassInfo.ExportedFileName, srcClassName) != nullptr)
			return true; // Already parsed

		ClassInfo classInfo;
		classInfo.NativeName = srcClassName;
		classInfo.NativeNameWithoutTemplateArguments = declName.str();
		classInfo.Visibility = parsedClassInfo.Visibility;
		classInfo.API = ParserUtility::ParseAPIFromExportFlags(parsedClassInfo.ExportFlags);
		classInfo.ClassFlags = 0;
		classInfo.BaseClassName = ScriptExportUtility::FindExportableBaseClassName(decl);
		classInfo.DocumentationGroup = parsedClassInfo.DocumentationGroup;
		classInfo.TemplateParameters = templParams;
		mCommentParser.ParseComments(templatedDecl, classInfo.Documentation);
		CommentParser::ClearParameterReferenceComments(classInfo.Documentation);

		parseNamespace(decl, classInfo.Namespace);

		if ((parsedClassInfo.style.flags & (int)StyleFlags::ForceHide) != 0)
			classInfo.ClassFlags |= (int)ClassFlags::HideInInspector;

		if (specDecl != nullptr)
			classInfo.ClassFlags |= (int)ClassFlags::IsTemplateInst;

		const bool typeIsBuiltinModuleType = ParserUtility::CheckIsBuiltinModuleType(decl);
		if (typeIsBuiltinModuleType)
			classInfo.ClassFlags |= (int)ClassFlags::IsModule;

		if (decl->isStruct())
			classInfo.ClassFlags |= (int)ClassFlags::IsStruct;

		::ExportedClassTypeCategory classType = DetermineExportedTypeCategory(decl);

		std::string declFile = astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()).str();
		TypeLookup::RegisterNativeToScriptTypeMapping(classInfo.Namespace, srcClassName, declFile, parsedClassInfo.ExportedTypeName, parsedClassInfo.ExportedFileName, classInfo.API, classType);

		std::stack<const CXXRecordDecl*> todo;
		todo.push(decl);

		while (!todo.empty())
		{
			const CXXRecordDecl* curDecl = todo.top();
			todo.pop();

			// Parse constructors for non-module (singleton) classes
			if (!typeIsBuiltinModuleType)
			{
				for (auto I = curDecl->ctor_begin(); I != curDecl->ctor_end(); ++I)
				{
					CXXConstructorDecl* ctorDecl = *I;

					AnnotateAttr* methodAttr = ctorDecl->getAttr<AnnotateAttr>();
					if (methodAttr == nullptr)
						continue;

					StringRef dummy;
					ScriptExportInformation parsedMethodInfo;
					if (!ScriptExportUtility::ParseExportAttribute(methodAttr, dummy, parsedMethodInfo))
						continue;

					MethodInfo methodInfo;
					methodInfo.NativeName = declName.str();
					methodInfo.ScriptName = parsedClassInfo.ExportedTypeName;
					methodInfo.MethodFlags = (int)MethodFlags::Constructor;
					methodInfo.Visibility = parsedMethodInfo.Visibility;
					methodInfo.API = ParserUtility::ParseAPIFromExportFlags(parsedMethodInfo.ExportFlags);
					mCommentParser.ParseComments(ctorDecl, methodInfo.Documentation);

					if ((parsedMethodInfo.ExportFlags & (int)ExportFlags::InteropOnly))
						methodInfo.MethodFlags |= (int)MethodFlags::InteropOnly;

					bool invalidParam = false;
					bool skippedDefaultArg = false;
					for (auto J = ctorDecl->param_begin(); J != ctorDecl->param_end(); ++J)
					{
						ParmVarDecl* paramDecl = *J;
						QualType paramType = paramDecl->getType();

						VariableInformation paramInfo;
						paramInfo.Name = paramDecl->getName().str();

						if (!ParseTypeInformation(paramType, paramInfo.TypeInformation))
						{
							outs() << "Error: Unable to parse parameter \"" << paramInfo.Name << "\" type in \"" << srcClassName << "\"'s constructor.\n";
							invalidParam = true;
							continue;
						}

						if (paramDecl->hasDefaultArg() && !skippedDefaultArg)
						{
							if (!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.DefaultValue, paramInfo.DefaultValueType))
							{
								outs() << "Error: Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
									<< "argument that cannot be constantly evaluated, ignoring it.\n";
								skippedDefaultArg = true;
							}
						}

						ParseParameterOrFieldAttribute(paramDecl, false, paramInfo.TypeInformation);
						methodInfo.Parameters.push_back(paramInfo);
					}

					if (invalidParam)
						continue;

					CommentParser::EnsureValidParameterReferenceComments(methodInfo.Parameters, methodInfo.Documentation);
					classInfo.Constructors.push_back(methodInfo);
				}
			}

			for (auto I = curDecl->method_begin(); I != curDecl->method_end(); ++I)
			{
				CXXMethodDecl* methodDecl = *I;

				CXXConstructorDecl* ctorDecl = dyn_cast<CXXConstructorDecl>(methodDecl);
				if (ctorDecl != nullptr)
					continue;

				if (!methodDecl->isUserProvided() || methodDecl->isImplicit())
					continue;

				AnnotateAttr* methodAttr = methodDecl->getAttr<AnnotateAttr>();
				if (methodAttr == nullptr)
					continue;

				StringRef sourceMethodName = methodDecl->getName();

				ScriptExportInformation parsedMethodInfo;
				if (!ScriptExportUtility::ParseExportAttribute(methodDecl, sourceMethodName, parsedMethodInfo))
					continue;

				if (methodDecl->getAccess() != AS_public)
					outs() << "Error: Exported method \"" + sourceMethodName + "\" isn't public. This will likely result in invalid code generation.";

				int methodFlags = 0;

				bool isExternal = false;
				if ((parsedMethodInfo.ExportFlags & (int)ExportFlags::ExternalMethod) != 0)
				{
					methodFlags |= (int)MethodFlags::External;
					isExternal = true;
				}

				if ((parsedMethodInfo.ExportFlags & (int)ExportFlags::ExternalConstructor) != 0)
				{
					methodFlags |= (int)MethodFlags::External;
					methodFlags |= (int)MethodFlags::Constructor;

					isExternal = true;
				}

				if ((parsedMethodInfo.ExportFlags & (int)ExportFlags::InteropOnly))
					methodFlags |= (int)MethodFlags::InteropOnly;

				bool isStatic = false;
				if (methodDecl->isStatic() && !isExternal) // Note: Perhaps add a way to mark external methods as static
				{
					methodFlags |= (int)MethodFlags::Static;
					isStatic = true;
				}

				if ((parsedMethodInfo.ExportFlags & (int)ExportFlags::PropertyGetter) != 0)
					methodFlags |= (int)MethodFlags::PropertyGetter;
				else if ((parsedMethodInfo.ExportFlags & (int)ExportFlags::PropertySetter) != 0)
					methodFlags |= (int)MethodFlags::PropertySetter;

				MethodInfo methodInfo;
				methodInfo.NativeName = sourceMethodName.str();
				methodInfo.ScriptName = parsedMethodInfo.ExportedTypeName;
				methodInfo.MethodFlags = methodFlags;
				methodInfo.ExternalClass = srcClassName;
				methodInfo.Visibility = parsedMethodInfo.Visibility;
				methodInfo.API = ParserUtility::ParseAPIFromExportFlags(parsedMethodInfo.ExportFlags);
				methodInfo.Style = parsedMethodInfo.style;
				mCommentParser.ParseComments(methodDecl, methodInfo.Documentation);

				bool isProperty = (parsedMethodInfo.ExportFlags & ((int)ExportFlags::PropertyGetter | (int)ExportFlags::PropertySetter));

				if (!isProperty)
				{
					QualType returnType = methodDecl->getReturnType();
					if (!returnType->isVoidType())
					{
						ReturnInfo returnInfo;
						if (!ParseTypeInformation(returnType, returnInfo.TypeInformation))
						{
							outs() << "Error: Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.\n";
							continue;
						}

						ParseParameterOrFieldAttribute(methodDecl, false, returnInfo.TypeInformation);
						methodInfo.ReturnValue = returnInfo;
					}
				}
				else
				{
					if ((parsedMethodInfo.ExportFlags & (int)ExportFlags::PropertyGetter) != 0)
					{
						QualType returnType = methodDecl->getReturnType();
						if (returnType->isVoidType())
						{
							outs() << "Error: Unable to create a getter for property because method \"" << sourceMethodName
								<< "\" has no return value.\n";
							continue;
						}

						// Note: I can potentially allow an output parameter instead of a return value
						if (methodDecl->param_size() > 1 || ((!isExternal || isStatic) && methodDecl->param_size() > 0))
						{
							outs() << "Error: Unable to create a getter for property because method \"" << sourceMethodName
								<< "\" has parameters.\n";
							continue;
						}

						if (!ParseTypeInformation(returnType, methodInfo.ReturnValue.TypeInformation))
						{
							outs() << "Error: Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.\n";
							continue;
						}

						ParseParameterOrFieldAttribute(methodDecl, false, methodInfo.ReturnValue.TypeInformation);
					}
					else // Must be setter
					{
						QualType returnType = methodDecl->getReturnType();
						if (!returnType->isVoidType())
						{
							outs() << "Error: Unable to create a setter for property because method \"" << sourceMethodName
								<< "\" has a return value.\n";
							continue;
						}

						if (methodDecl->param_size() == 0 || methodDecl->param_size() > 2 || ((!isExternal || isStatic) && methodDecl->param_size() != 1))
						{
							outs() << "Error: Unable to create a setter for property because method \"" << sourceMethodName
								<< "\" has more or less than one parameter.\n";
							continue;
						}

						ParmVarDecl* paramDecl = methodDecl->getParamDecl(isExternal ? 1 : 0);

						VariableInformation paramInfo;
						paramInfo.Name = paramDecl->getName().str();

						if (!ParseTypeInformation(paramDecl->getType(), paramInfo.TypeInformation))
						{
							outs() << "Error: Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.\n";
							continue;
						}
					}
				}

				bool invalidParam = false;
				bool skippedDefaultArg = false;
				for (auto J = methodDecl->param_begin(); J != methodDecl->param_end(); ++J)
				{
					ParmVarDecl* paramDecl = *J;
					QualType paramType = paramDecl->getType();

					VariableInformation parameterInformation;
					parameterInformation.Name = paramDecl->getName().str();

					if (!ParseTypeInformation(paramType, parameterInformation.TypeInformation))
					{
						outs() << "Error: Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.\n";
						invalidParam = true;
						continue;
					}

					if (paramDecl->hasDefaultArg() && !skippedDefaultArg)
					{
						Expr* defaultArg;
						if (paramDecl->hasUninstantiatedDefaultArg())
							defaultArg = paramDecl->getUninstantiatedDefaultArg();
						else
							defaultArg = paramDecl->getDefaultArg();

						if (!evaluateExpression(defaultArg, parameterInformation.DefaultValue, parameterInformation.DefaultValueType))
						{
							outs() << "Error: Method parameter \"" << paramDecl->getName().str() << "\" has a default "
								<< "argument that cannot be constantly evaluated, ignoring it.\n";
							skippedDefaultArg = true;
						}
					}

					ParseParameterOrFieldAttribute(paramDecl, false, parameterInformation.TypeInformation);
					methodInfo.Parameters.push_back(parameterInformation);
				}

				if (invalidParam)
					continue;

				CommentParser::EnsureValidParameterReferenceComments(methodInfo.Parameters, methodInfo.Documentation);

				if (isExternal)
				{
					if (parsedMethodInfo.ExtensionOfType == "T")
						parsedMethodInfo.ExtensionOfType = srcClassName;

					ExternalClassInfos& infos = externalClassInfos[parsedMethodInfo.ExtensionOfType];
					infos.Methods.push_back(methodInfo);
				}
				else
					classInfo.Methods.push_back(methodInfo);
			}

			// Look for exported fields & events
			for (auto I = curDecl->field_begin(); I != curDecl->field_end(); ++I)
			{
				FieldDecl* fieldDecl = *I;

				MethodInfo eventInfo;
				if (parseEvent(fieldDecl, srcClassName, eventInfo))
					classInfo.Events.push_back(eventInfo);
				else
				{
					FieldInfo fieldInfo;
					fieldInfo.Name = fieldDecl->getName().str();

					ScriptExportInformation parsedFieldInfo;
					bool foundExportAttrib = false;
					for(const auto& entry : fieldDecl->specific_attrs<AnnotateAttr>())
					{
						if(ScriptExportUtility::IsExportAttribute(entry))
						{
							if (ScriptExportUtility::ParseExportAttribute(entry, fieldInfo.Name, parsedFieldInfo))
								foundExportAttrib = true;

							break;
						}
					}

					if(!foundExportAttrib)
						continue;

					std::string typeName;
					if (!ParseTypeInformation(fieldDecl->getType(), fieldInfo.TypeInformation))
					{
						outs() << "Error: Unable to detect type for field \"" << fieldDecl->getName().str() << "\" in \""
							<< srcClassName << "\". Skipping field.\n";
						continue;
					}

					if (fieldDecl->getAccess() != AS_public)
						outs() << "Error: Exported field \"" + fieldInfo.Name + "\" isn't public. This will likely result in invalid code generation.";

					fieldInfo.Style = parsedFieldInfo.style;

					mCommentParser.ParseComments(fieldDecl, fieldInfo.Documentation);
					CommentParser::ClearParameterReferenceComments(fieldInfo.Documentation);

					classInfo.Fields.push_back(fieldInfo);

					// Register wrapper methods, this way we can re-use much of the same logic for method/property generation
					MethodInfo getterInfo;
					getterInfo.NativeName = "Get" + fieldInfo.Name;
					getterInfo.ScriptName = parsedFieldInfo.ExportedTypeName;
					getterInfo.Visibility = parsedFieldInfo.Visibility;
					getterInfo.API = ParserUtility::ParseAPIFromExportFlags(parsedFieldInfo.ExportFlags);
					getterInfo.MethodFlags = (int)MethodFlags::PropertyGetter | (int)MethodFlags::FieldWrapper;
					getterInfo.Style = fieldInfo.Style;

					getterInfo.ReturnValue.TypeInformation = fieldInfo.TypeInformation;
					ParseParameterOrFieldAttribute(fieldDecl, true, getterInfo.ReturnValue.TypeInformation);

					if ((parsedFieldInfo.ExportFlags & (int)ExportFlags::InteropOnly) != 0)
						getterInfo.MethodFlags |= (int)MethodFlags::InteropOnly;

					VariableInformation paramInfo;
					paramInfo.TypeInformation = fieldInfo.TypeInformation;
					paramInfo.Name = "value";

					ParseParameterOrFieldAttribute(fieldDecl, true, paramInfo.TypeInformation);

					MethodInfo setterInfo;
					setterInfo.NativeName = "Set" + fieldInfo.Name;
					setterInfo.ScriptName = parsedFieldInfo.ExportedTypeName;
					setterInfo.Documentation = fieldInfo.Documentation;
					setterInfo.Parameters.push_back(paramInfo);
					setterInfo.Visibility = parsedFieldInfo.Visibility;
					setterInfo.API = ParserUtility::ParseAPIFromExportFlags(parsedFieldInfo.ExportFlags);
					setterInfo.MethodFlags = (int)MethodFlags::PropertySetter | (int)MethodFlags::FieldWrapper;
					setterInfo.Style = fieldInfo.Style;

					if ((parsedFieldInfo.ExportFlags & (int)ExportFlags::InteropOnly) != 0)
						setterInfo.MethodFlags |= (int)MethodFlags::InteropOnly;

					classInfo.Methods.push_back(getterInfo);
					classInfo.Methods.push_back(setterInfo);
				}
			}

			// Find static data events
			const DeclContext* context = dyn_cast<DeclContext>(curDecl);
			for (auto I = context->decls_begin(); I != context->decls_end(); ++I)
			{
				if (VarDecl* varDecl = dyn_cast<VarDecl>(*I))
				{
					if (!varDecl->isStaticDataMember())
						continue;

					MethodInfo eventInfo;
					if (!parseEvent(varDecl, srcClassName, eventInfo))
						continue;

					eventInfo.MethodFlags |= (int)MethodFlags::Static;
					classInfo.Events.push_back(eventInfo);
				}
			}

			auto iter = curDecl->bases_begin();
			while (iter != curDecl->bases_end())
			{
				const CXXBaseSpecifier* baseSpec = iter;
				CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

				// Base classes never need to be exported. Exportable classes will handle their own methods/fields.
				if (ParserUtility::IsBuiltinBaseType(baseDecl) || ScriptExportUtility::IsExportable(baseDecl))
				{
					iter++;
					continue;
				}

				todo.push(baseDecl);
				iter++;
			}
		}

		// External classes are just containers for external methods, we don't need to process them directly
		if ((parsedClassInfo.ExportFlags & (int)ExportFlags::ExternalMethod) == 0)
		{
			TypeLookup::RegisterEntryToGenerate(parsedClassInfo.ExportedFileName, classInfo);
		}
	}

	return true;
}
