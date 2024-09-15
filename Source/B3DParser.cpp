#include "B3DParser.h"
#include "B3DParserUtility.h"
#include "B3DScriptExportAttributeParser.h"
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

				if (className == kBuiltinGameObjectType)
					return ::ExportedClassTypeCategory::GameObject;
				else if (className == kBuiltinComponentType)
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
static bool ParseParameterOrFieldAttribute(Decl* decl, bool isField, VariableTypeInformation& typeInformation)
{
	for(const auto& entry : decl->specific_attrs<AnnotateAttr>())
	{
		if (!isField && entry->getAnnotation() == "params")
		{
			typeInformation.SetParameterFlag(ParameterFlags::VarParams, true);
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

/** Parses the namespace of the declaration and stores it in @p output. */
void ParseNamespace(NamedDecl* decl, SmallVector<std::string, 4>& output)
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

BansheeCodeGeneratorASTVisitor::BansheeCodeGeneratorASTVisitor(CompilerInstance* CI, CommentParser& commentParser)
	:astContext(&(CI->getASTContext())), preprocessor(CI->getPreprocessor()), mCommentParser(commentParser)
{ }

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
			else if(sourceTypeName == "TArray")
			{
				QualType aliasedType = specType->getAliasedType();
				specType = aliasedType->getAs<TemplateSpecializationType>(); // TODO - Move this above, we always want to work on the aliased type

				QualType allocatorType = specType->getArg(1).getAsType();
				VariableTypeInformation allocatorTypeInformation;
				if (!ParseTypeInformation(allocatorType, allocatorTypeInformation))
				{
					outs() << "Error: Failed parsing underlying TArray<T> allocator.\n";
					return false;
				}

				const RecordType* allocatorRecordType = allocatorType->getAs<RecordType>();
				const RecordDecl* allocatorRecordDecl = allocatorRecordType->getDecl();
				std::string allocatorTypeName = allocatorRecordDecl->getName().str();

				if(allocatorTypeName == "InlineContainerAllocator")
				{
					outType.TypeName = "TInlineArray";
					outType.TypeCategory = VariableTypeCategory::TInlineArray;

					const TemplateSpecializationType* allocatorSpecializationType = allocatorType->getAs<TemplateSpecializationType>();

					uint32_t inlineVectorSize = 0;
					if(allocatorSpecializationType->getNumArgs() > 0)
					{
						std::string tmplArgExprValue, exprType;
						if (TryEvaluateExpression(allocatorSpecializationType->getArg(0).getAsExpr(), tmplArgExprValue, exprType))
						{
							try
							{
								inlineVectorSize = std::stoi(tmplArgExprValue);
							}
							catch(const std::invalid_argument& ex)
							{
								errs() << "Error: Cannot convert TInlineArray size template argument to a number, ignoring it.\n";
							}
							catch(const std::out_of_range& ex)
							{
								errs() << "Error: Cannot convert TInlineArray size template argument to a number, ignoring it.\n";
							}
							
						}
						else
							errs() << "Error: Template argument for TInlineArray cannot be constantly evaluated, ignoring it.\n";
					}

					outType.ArraySize = inlineVectorSize;
				}
				else
				{
					outType.TypeName = "TArray";
					outType.TypeCategory = VariableTypeCategory::TArray;
				}

				QualType underlyingType = specType->getArg(0).getAsType();
				VariableTypeInformation underlyingTypeInformation;
				if (!ParseTypeInformation(underlyingType, underlyingTypeInformation))
				{
					errs() << "Error: Failed parsing underlying TArray<T> type.\n";
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

		// Handle specialized template types
		const ClassTemplateSpecializationDecl* const specDecl = dyn_cast<ClassTemplateSpecializationDecl>(recordDecl);
		if (specDecl != nullptr)
		{
			auto& templateInstantiationArguments = specDecl->getTemplateInstantiationArgs();
			sourceTypeName += ParseTemplateArguments(sourceTypeName, templateInstantiationArguments.data(), templateInstantiationArguments.size(), nullptr);
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

bool BansheeCodeGeneratorASTVisitor::TryParseEventSignature(QualType type, MethodInfo& outEventInformation, bool& outIsCallback)
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
			{
				isEvent = true;
				outIsCallback = false;
			}
			else if(sourceTypeName == "function" && recordDecl->isInStdNamespace())
			{
				isEvent = true;
				outIsCallback = true;
			}

			if (isEvent)
			{
				type = specType->getArg(0).getAsType();
				if(type->isFunctionProtoType())
				{
					const FunctionProtoType* funcType = type->getAs<FunctionProtoType>();

					unsigned int numParams = funcType->getNumParams();
					outEventInformation.Parameters.resize(numParams);

					for(unsigned int i = 0; i < numParams; i++)
					{
						QualType paramType = funcType->getParamType(i);
						ParseTypeInformation(paramType, outEventInformation.Parameters[i].TypeInformation);
					}

					QualType returnType = funcType->getReturnType();
					if (!returnType->isVoidType())
						ParseTypeInformation(returnType, outEventInformation.ReturnValue.TypeInformation);
				}

				return true;
			}
		}
	}

	return false;
}

bool BansheeCodeGeneratorASTVisitor::TryEvaluateLiteral(Expr* expression, std::string& evaluatedValue)
{
	QualType type = expression->getType();
	if (type->isBuiltinType())
	{
		const BuiltinType* builtinType = type->getAs<BuiltinType>();
		switch (builtinType->getKind())
		{
		case BuiltinType::Bool:
		{
			bool result;
			expression->EvaluateAsBooleanCondition(result, *astContext);

			evaluatedValue = result ? "true" : "false";

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
			expression->EvaluateAsInt(result, *astContext);

			SmallString<5> valueStr;

			result.Val.getInt().toString(valueStr);
			evaluatedValue = valueStr.str().str();

			return true;
		}
		case BuiltinType::Float:
		{
			APFloat result(0.0f);
			expression->EvaluateAsFloat(result, *astContext);

			SmallString<8> valueStr;
			result.toString(valueStr);
			evaluatedValue = valueStr.str().str() + "f";

			return true;
		}
		case BuiltinType::Double:
		{
			APFloat result(0.0f);
			expression->EvaluateAsFloat(result, *astContext);

			SmallString<8> valueStr;
			result.toString(valueStr);
			evaluatedValue = valueStr.str().str();

			return true;
		}
		case BuiltinType::NullPtr:
		{
			evaluatedValue = "null";
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
		expression->EvaluateAsInt(result, *astContext);

		SmallString<5> valueStr;
		result.Val.getInt().toString(valueStr);
		evaluatedValue = valueStr.str().str();

		return true;
	}

	return false;
}

bool BansheeCodeGeneratorASTVisitor::TryEvaluateExpression(Expr* expression, std::string& outEvaluatedValue, std::string& outEvaluatedValueType)
{
	if (expression->isEvaluatable(*astContext))
	{
		if (TryEvaluateLiteral(expression, outEvaluatedValue))
			return true;
	}

	// Check for nullptr, literals in constructors and cast literals
	if (ExprWithCleanups* cleanups = dyn_cast<ExprWithCleanups>(expression))
		expression = cleanups->getSubExpr();

	// Skip through reference binding to temporary.
	if (MaterializeTemporaryExpr* materialize = dyn_cast<MaterializeTemporaryExpr>(expression))
		expression = materialize->getSubExpr();

	// Skip casts. e.g. `const GUIContent& content = GUIContent()` will result in a cast
	if(ImplicitCastExpr* castExpr = dyn_cast<ImplicitCastExpr>(expression))
		expression = castExpr->getSubExpr();

	// Skip any temporary bindings; they're implicit.
	if (CXXBindTemporaryExpr* binder = dyn_cast<CXXBindTemporaryExpr>(expression))
		expression = binder->getSubExpr();

	expression = expression->IgnoreParenCasts();

	// Reference to some other declaration (e.g. a static)
	DeclRefExpr* declRefExpr = dyn_cast<DeclRefExpr>(expression);
	if(declRefExpr)
	{
		ValueDecl* decl = declRefExpr->getDecl();
		const std::string name = ParserUtility::GetFullName(decl);

		if(name == (sFrameworkCppNs + "::StringUtil::kBlank") || name == (sFrameworkCppNs + "::StringUtil::kWblank"))
		{
			outEvaluatedValue = "\"\"";
			outEvaluatedValueType = "";
			return true;
		}
		else if(name == (sFrameworkCppNs + "::UUID::kEmpty"))
		{
			outEvaluatedValue = "";
			outEvaluatedValueType = "UUID";
			return true;
		}
	}

	CXXConstructExpr* ctorExp = dyn_cast<CXXConstructExpr>(expression);
	if (!ctorExp)
		return false;

	// Check for special case of a single null parameter
	if(ctorExp->getNumArgs() > 0)
	{
		expression = ctorExp->getArg(0);

		bool isNull = false;
		QualType type = expression->getType();
		if (type->isBuiltinType())
		{
			const BuiltinType* builtinType = type->getAs<BuiltinType>();
			if (builtinType->getKind() == BuiltinType::NullPtr)
			{
				outEvaluatedValue = "null";
				return true;
			}
		}
	}

	// Constructor or cast of some type
	QualType parentType = ctorExp->getType();

	VariableBase variableInformation;
	ParseTypeInformation(parentType, variableInformation.TypeInformation);
	outEvaluatedValueType = variableInformation.TypeInformation.GetLastWrappedOrSelfTypeName();

	for(int i = 0; i < ctorExp->getNumArgs(); i++)
	{
		if (i != 0)
			outEvaluatedValue += ", ";

		std::string argValue;
		expression = ctorExp->getArg(i);

		bool isNull = false;
		QualType type = expression->getType();
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
			if (expression->isEvaluatable(*astContext))
			{
				if (!TryEvaluateLiteral(expression, argValue))
					return false;
			}
			else
			{
				std::string dummy3;
				if (!TryEvaluateExpression(expression, argValue, dummy3))
					return false;
			}
		}
			
		outEvaluatedValue += argValue;
	}

	return true;
}

bool BansheeCodeGeneratorASTVisitor::TryParseEvent(ValueDecl* decl, const std::string& className, MethodInfo& outEventInformation)
{
	AnnotateAttr* fieldAttr = decl->getAttr<AnnotateAttr>();
	if (fieldAttr == nullptr)
		return false;

	StringRef sourceFieldName = decl->getName();

	ScriptExportInformation parsedEventInfo;
	if (!ScriptExportAttributeParser::ParseExportAttribute(fieldAttr, sourceFieldName, parsedEventInfo))
		return false;

	MethodInfo eventSignature;
	bool isCallback = false;
	if (!TryParseEventSignature(decl->getType(), eventSignature, isCallback))
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

	outEventInformation.NativeName = sourceFieldName.str();
	outEventInformation.ScriptName = parsedEventInfo.ExportedTypeName;
	outEventInformation.MethodFlags = eventFlags;
	outEventInformation.ExternalClass = className;
	outEventInformation.Visibility = parsedEventInfo.Visibility;
	outEventInformation.API = ParserUtility::ParseAPIFromExportFlags(parsedEventInfo.ExportFlags);
	mCommentParser.ParseComments(decl, outEventInformation.Documentation);
	CommentParser::ClearParameterReferenceComments(outEventInformation.Documentation);

	if (!eventSignature.ReturnValue.TypeInformation.IsEmpty())
	{
		outEventInformation.ReturnValue.TypeInformation = eventSignature.ReturnValue.TypeInformation;
	}

	int idx = 0;
	for(auto& entry : eventSignature.Parameters)
	{
		VariableInformation paramInfo;
		paramInfo.Name = "p" + std::to_string(idx);
		paramInfo.TypeInformation = entry.TypeInformation;

		outEventInformation.Parameters.push_back(paramInfo);
		idx++;
	}

	return true;
}

std::string BansheeCodeGeneratorASTVisitor::ParseTemplateArguments(const std::string& className, const TemplateArgument* arguments, uint32_t argumentCount, SmallVector<TemplateParamInfo, 0>* outTemplateArgumentInformation)
{
	std::stringstream tmplArgsStream;
	tmplArgsStream << "<";
	for(unsigned i = 0; i < argumentCount; i++)
	{
		if (i != 0)
			tmplArgsStream << ", ";

		auto& tmplArg = arguments[i];
		if(tmplArg.getKind() == TemplateArgument::Type)
		{
			VariableBase variableInformation;
			ParseTypeInformation(tmplArg.getAsType(), variableInformation.TypeInformation);

			tmplArgsStream << variableInformation.TypeInformation.GetLastWrappedOrSelfTypeName();

			if(outTemplateArgumentInformation != nullptr)
				outTemplateArgumentInformation->push_back({ "class" });
		}
		else if(tmplArg.getKind() == TemplateArgument::Expression)
		{
			std::string tmplArgExprValue, exprType;
			if (!TryEvaluateExpression(tmplArg.getAsExpr(), tmplArgExprValue, exprType))
			{
				outs() << "Error: Template argument for type \"" << className << "\" cannot be constantly evaluated, ignoring it.\n";
				tmplArgsStream << "unknown";
			}
			else
				tmplArgsStream << tmplArgExprValue;

			VariableBase variableInformation;
			ParseTypeInformation(tmplArg.getAsExpr()->getType(), variableInformation.TypeInformation);

			if(outTemplateArgumentInformation != nullptr)
				outTemplateArgumentInformation->push_back({ variableInformation.TypeInformation.GetLastWrappedOrSelfTypeName() });
		}
		else
		{
			outs() << "Error: Cannot parse template argument for type: \"" << className << "\". \n";
			tmplArgsStream << "unknown";

			if(outTemplateArgumentInformation != nullptr)
				outTemplateArgumentInformation->push_back({ "unknown" });
		}
	}

	tmplArgsStream << ">";
	return tmplArgsStream.str();
}

bool BansheeCodeGeneratorASTVisitor::TryParseDeclarationAsStruct(CXXRecordDecl* declaration, const ScriptExportInformation& scriptExportInformation, StructInfo& outStructInfo)
{
	StringRef declarationName = declaration->getName();
	std::string sourceClassName = declarationName.str();

	// If a template specialization append template params to its name
	ClassTemplateSpecializationDecl* specializationDeclaration = dyn_cast<ClassTemplateSpecializationDecl>(declaration);
	CXXRecordDecl* templatedDeclaration = declaration;
	SmallVector<TemplateParamInfo, 0> templateParameters;
	if(specializationDeclaration != nullptr)
	{
		auto& templateInstantiationArguments = specializationDeclaration->getTemplateInstantiationArgs();
		sourceClassName += ParseTemplateArguments(sourceClassName, templateInstantiationArguments.data(), templateInstantiationArguments.size(), &templateParameters);
		templatedDeclaration = specializationDeclaration->getSpecializedTemplate()->getTemplatedDecl();
	}

	if (TypeLookup::FindStructInformationInFile(scriptExportInformation.ExportedFileName, sourceClassName) != nullptr)
		return false; // Already parsed

	outStructInfo.NativeName = sourceClassName;
	outStructInfo.NativeNameWithoutTemplateArguments = declarationName.str();
	outStructInfo.BaseClassName = ScriptExportAttributeParser::FindExportableBasePlainClassName(declaration);
	outStructInfo.Visibility = scriptExportInformation.Visibility;
	outStructInfo.RequiresInteropType = declaration->isPolymorphic();
	outStructInfo.DocumentationGroup = scriptExportInformation.DocumentationGroup;
	outStructInfo.IsTemplateInstatiation = specializationDeclaration != nullptr;
	outStructInfo.TemplateParameters = templateParameters;
	outStructInfo.API = ParserUtility::ParseAPIFromExportFlags(scriptExportInformation.ExportFlags);

	mCommentParser.ParseComments(templatedDeclaration, outStructInfo.Documentation);
	ParseNamespace(declaration, outStructInfo.Namespace);
	CommentParser::ClearParameterReferenceComments(outStructInfo.Documentation);

	std::unordered_map<FieldDecl*, std::pair<std::string, std::string>> defaultFieldValues;

	// Parses assignment operations in the provided method body and outputs it to @p outAssignments map
	auto fnParseAssignmentsInBody = [&sourceClassName](const CXXMethodDecl& methodDecl, std::unordered_map<FieldDecl*, ParmVarDecl*> outAssignments)
	{
		// Parse any assignments in the function body
		// Note: Searching for trivially simple assignments only, ignoring anything else
		if(!methodDecl.hasBody())
			return;

		CompoundStmt* functionBody = dyn_cast<CompoundStmt>(methodDecl.getBody()); // Note: Not handling inner blocks
		assert(functionBody != nullptr);

		for(auto I = functionBody->child_begin(); I != functionBody->child_end(); ++I)
		{
			Stmt* stmt = *I;

			BinaryOperator* binaryOp = dyn_cast<BinaryOperator>(stmt);
			if(binaryOp == nullptr)
				continue;

			if(binaryOp->getOpcode() != BO_Assign)
				continue;

			Expr* lhsExpr = binaryOp->getLHS()->IgnoreParenCasts(); // Note: Ignoring even explicit casts
			Decl* lhsDecl;

			if(DeclRefExpr* varExpr = dyn_cast<DeclRefExpr>(lhsExpr))
				lhsDecl = varExpr->getDecl();
			else if(MemberExpr* memberExpr = dyn_cast<MemberExpr>(lhsExpr))
				lhsDecl = memberExpr->getMemberDecl();
			else
				continue;

			FieldDecl* fieldDecl = dyn_cast<FieldDecl>(lhsDecl);
			if(fieldDecl == nullptr)
				continue;

			Expr* rhsExpr = binaryOp->getRHS()->IgnoreParenCasts();
			Decl* rhsDecl = nullptr;

			if(DeclRefExpr* varExpr = dyn_cast<DeclRefExpr>(rhsExpr))
				rhsDecl = varExpr->getDecl();
			else if(MemberExpr* memberExpr = dyn_cast<MemberExpr>(rhsExpr))
				rhsDecl = memberExpr->getMemberDecl();

			ParmVarDecl* parmVarDecl = nullptr;
			if(rhsDecl != nullptr)
				parmVarDecl = dyn_cast<ParmVarDecl>(rhsDecl);

			if(parmVarDecl == nullptr)
			{
				outs() << "Warning: Found a non-trivial field assignment for field \"" << fieldDecl->getName() << "\" in"
					<< " constructor of \"" << sourceClassName << "\". Ignoring assignment.\n";
				continue;
			}

			outAssignments[fieldDecl] = parmVarDecl;
		}
	};

	// Parses information about every parameter in the method, and outputs information about parameters in @p outParameters.
	auto fnParseMethodParameters = [this](const CXXMethodDecl& methodDecl, std::vector<VariableInformation>& outParameters) {
		bool skippedDefaultArgument = false;
		for (auto I = methodDecl.param_begin(); I != methodDecl.param_end(); ++I)
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
				if (!TryEvaluateExpression(paramDecl->getDefaultArg(), paramInfo.DefaultValue, paramInfo.DefaultValueType))
				{
					outs() << "Error: Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
						<< "argument that cannot be constantly evaluated, ignoring it.\n";
					skippedDefaultArgument = true;
				}
			}

			ParseParameterOrFieldAttribute(paramDecl, false, paramInfo.TypeInformation);
			outParameters.push_back(paramInfo);
		}
	};

	// Parse non-default constructors & determine default values for fields
	if (declaration->hasUserDeclaredConstructor())
	{
		auto ctorIter = declaration->ctor_begin();
		while (ctorIter != declaration->ctor_end())
		{
			StructConstructorInfo ctorInfo;
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
				ScriptExportAttributeParser::ParseExportAttribute(ctorAttr, sourceClassName, parsedCtorInfo);

				if ((parsedCtorInfo.ExportFlags & (int)ExportFlags::Exclude) != 0)
				{
					++ctorIter;
					continue;
				}
			}

			mCommentParser.ParseComments(ctorDecl, ctorInfo.Documentation);

			fnParseMethodParameters(*ctorDecl, ctorInfo.Parameters);
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
								"constructors are supported. In struct \"" + sourceClassName + "\".\n";
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
						if (TryEvaluateExpression(initExpr, evalValue, evalTypeValue))
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

								outs() << "Error: Unrecognized initializer format in struct \"" << sourceClassName << "\" for field \"" << fieldName << "\".\n";
							}
						}
					}
				}
			}

			fnParseAssignmentsInBody(*ctorDecl, assignments);

			for (auto I = declaration->field_begin(); I != declaration->field_end(); ++I)
			{
				auto iterFind = assignments.find(*I);
				if (iterFind == assignments.end())
					continue;

				std::string fieldName = iterFind->first->getName().str();
				std::string paramName = iterFind->second->getName().str();

				ctorInfo.FieldAssignments[fieldName] = paramName;
			}

			CommentParser::EnsureValidParameterReferenceComments(ctorInfo.Parameters, ctorInfo.Documentation);

			outStructInfo.Constructors.push_back(ctorInfo);
			++ctorIter;
		}
	}

	// Look for external constructors
	// Note: This is not fully implemented. We're not parsing obj.field = param assignments, just field = param.
	for (auto I = declaration->method_begin(); I != declaration->method_end(); ++I)
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
		if (!ScriptExportAttributeParser::ParseExportAttribute(methodDecl, sourceMethodName, parsedMethodInfo))
			continue;

		if((parsedMethodInfo.ExportFlags & (int)ExportFlags::ExternalConstructor) == 0)
			continue;

		if (methodDecl->getAccess() != AS_public)
			outs() << "Error: Exported method \"" + sourceMethodName + "\" isn't public. This will likely result in invalid code generation.";

		StructConstructorInfo ctorInfo;

		mCommentParser.ParseComments(ctorDecl, ctorInfo.Documentation);

		fnParseMethodParameters(*ctorDecl, ctorInfo.Parameters);

		std::unordered_map<FieldDecl*, ParmVarDecl*> assignments;
		fnParseAssignmentsInBody(*ctorDecl, assignments);

		for (auto I = declaration->field_begin(); I != declaration->field_end(); ++I)
		{
			auto iterFind = assignments.find(*I);
			if (iterFind == assignments.end())
				continue;

			std::string fieldName = iterFind->first->getName().str();
			std::string paramName = iterFind->second->getName().str();

			ctorInfo.FieldAssignments[fieldName] = paramName;
		}

		CommentParser::EnsureValidParameterReferenceComments(ctorInfo.Parameters, ctorInfo.Documentation);

		ctorInfo.StaticMethodName = sourceMethodName.str();
		outStructInfo.Constructors.push_back(ctorInfo);
	}

	std::stack<const CXXRecordDecl*> todo;
	todo.push(declaration);

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
			if (ScriptExportAttributeParser::ParseExportAttribute(fieldDecl, sourceClassName, parsedFieldInfo))
			{
				if ((parsedFieldInfo.ExportFlags & (int)ExportFlags::Exclude) != 0)
				{
					outStructInfo.RequiresInteropType = true;
					continue;
				}

				fieldInfo.MetaData = parsedFieldInfo.MetaData;
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

				if (initExpr != nullptr)
				{
					TryEvaluateExpression(initExpr, fieldInfo.DefaultValue, fieldInfo.DefaultValueType);
				}
			}

			std::string typeName;
			if (!ParseTypeInformation(fieldDecl->getType(), fieldInfo.TypeInformation))
			{
				outs() << "Error: Unable to detect type for field \"" << fieldDecl->getName().str() << "\" in \""
					<< sourceClassName << "\". Skipping field.\n";
				continue;
			}

			ParseParameterOrFieldAttribute(fieldDecl, true, fieldInfo.TypeInformation);

			// Remove the pass-as-resource-ref flag to all parameters initializing the field
			if(!fieldInfo.TypeInformation.IsParameterFlagSet(ParameterFlags::AsResourceRef))
			{
				for(auto& ctorInfo : outStructInfo.Constructors)
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

			outStructInfo.Fields.push_back(fieldInfo);
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
	if (outStructInfo.Constructors.empty() && hasDefaultValue)
		outStructInfo.Constructors.push_back(StructConstructorInfo());

	return true;
}

bool BansheeCodeGeneratorASTVisitor::TryParseDeclarationAsClass(CXXRecordDecl* declaration, const ScriptExportInformation& scriptExportInformation, ClassInfo& outClassInfo)
{
	StringRef declarationName = declaration->getName();
	std::string sourceClassName = declarationName.str();

	// If a template specialization append template params to its name
	ClassTemplateSpecializationDecl* specializationDeclaration = dyn_cast<ClassTemplateSpecializationDecl>(declaration);
	CXXRecordDecl* templatedDeclaration = declaration;
	SmallVector<TemplateParamInfo, 0> templateParameters;
	if(specializationDeclaration != nullptr)
	{
		auto& templateInstantiationArguments = specializationDeclaration->getTemplateInstantiationArgs();
		sourceClassName += ParseTemplateArguments(sourceClassName, templateInstantiationArguments.data(), templateInstantiationArguments.size(), &templateParameters);
		templatedDeclaration = specializationDeclaration->getSpecializedTemplate()->getTemplatedDecl();
	}

	if(TypeLookup::FindClassInformationInFile(scriptExportInformation.ExportedFileName, sourceClassName) != nullptr)
		return false; // Already parsed

	outClassInfo.NativeName = sourceClassName;
	outClassInfo.NativeNameWithoutTemplateArguments = declarationName.str();
	outClassInfo.Visibility = scriptExportInformation.Visibility;
	outClassInfo.API = ParserUtility::ParseAPIFromExportFlags(scriptExportInformation.ExportFlags);
	outClassInfo.ClassFlags = 0;
	outClassInfo.BaseClassName = ScriptExportAttributeParser::FindExportableBaseClassName(declaration);
	outClassInfo.DocumentationGroup = scriptExportInformation.DocumentationGroup;
	outClassInfo.TemplateParameters = templateParameters;
	mCommentParser.ParseComments(templatedDeclaration, outClassInfo.Documentation);
	CommentParser::ClearParameterReferenceComments(outClassInfo.Documentation);

	ParseNamespace(declaration, outClassInfo.Namespace);

	if((scriptExportInformation.MetaData.Flags & (int)MetaDataFlags::ForceHideInInspector) != 0)
		outClassInfo.ClassFlags |= (int)ClassFlags::HideInInspector;

	if(specializationDeclaration != nullptr)
		outClassInfo.ClassFlags |= (int)ClassFlags::IsTemplateInst;

	const bool typeIsBuiltinModuleType = ParserUtility::CheckIsBuiltinModuleType(declaration);
	if(typeIsBuiltinModuleType)
	{
		outClassInfo.ClassFlags |= (int)ClassFlags::IsModule;
		outClassInfo.ClassFlags |= (int)ClassFlags::UsesIScriptExportableAPI;
	}

	if(declaration->isStruct())
		outClassInfo.ClassFlags |= (int)ClassFlags::IsStruct;

	::ExportedClassTypeCategory classType = DetermineExportedTypeCategory(declaration);

	if(ParserUtility::HasIScriptExportableBaseClass(declaration))
		outClassInfo.ClassFlags |= (int)ClassFlags::UsesIScriptExportableAPI;

	std::string declFile = astContext->getSourceManager().getFilename(declaration->getSourceRange().getBegin()).str();
	TypeLookup::RegisterNativeToScriptTypeMapping(outClassInfo.Namespace, sourceClassName, declFile, scriptExportInformation.ExportedTypeName, scriptExportInformation.ExportedFileName, outClassInfo.API, classType);

	std::stack<const CXXRecordDecl*> todo;
	todo.push(declaration);

	while(!todo.empty())
	{
		const CXXRecordDecl* curDecl = todo.top();
		todo.pop();

		// Parse constructors for non-module (singleton) classes
		if(!typeIsBuiltinModuleType)
		{
			for(auto I = curDecl->ctor_begin(); I != curDecl->ctor_end(); ++I)
			{
				CXXConstructorDecl* ctorDecl = *I;

				AnnotateAttr* methodAttr = ctorDecl->getAttr<AnnotateAttr>();
				if(methodAttr == nullptr)
					continue;

				StringRef dummy;
				ScriptExportInformation parsedMethodInfo;
				if(!ScriptExportAttributeParser::ParseExportAttribute(methodAttr, dummy, parsedMethodInfo))
					continue;

				MethodInfo methodInfo;
				methodInfo.NativeName = declarationName.str();
				methodInfo.ScriptName = scriptExportInformation.ExportedTypeName;
				methodInfo.MethodFlags = (int)MethodFlags::Constructor;
				methodInfo.Visibility = parsedMethodInfo.Visibility;
				methodInfo.API = ParserUtility::ParseAPIFromExportFlags(parsedMethodInfo.ExportFlags);
				mCommentParser.ParseComments(ctorDecl, methodInfo.Documentation);

				if((parsedMethodInfo.ExportFlags & (int)ExportFlags::InteropOnly))
					methodInfo.MethodFlags |= (int)MethodFlags::InteropOnly;

				bool invalidParam = false;
				bool skippedDefaultArg = false;
				for(auto J = ctorDecl->param_begin(); J != ctorDecl->param_end(); ++J)
				{
					ParmVarDecl* paramDecl = *J;
					QualType paramType = paramDecl->getType();

					VariableInformation paramInfo;
					paramInfo.Name = paramDecl->getName().str();

					if(!ParseTypeInformation(paramType, paramInfo.TypeInformation))
					{
						outs() << "Error: Unable to parse parameter \"" << paramInfo.Name << "\" type in \"" << sourceClassName << "\"'s constructor.\n";
						invalidParam = true;
						continue;
					}

					if(paramDecl->hasDefaultArg() && !skippedDefaultArg)
					{
						if(!TryEvaluateExpression(paramDecl->getDefaultArg(), paramInfo.DefaultValue, paramInfo.DefaultValueType))
						{
							outs() << "Error: Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
								   << "argument that cannot be constantly evaluated, ignoring it.\n";
							skippedDefaultArg = true;
						}
					}

					ParseParameterOrFieldAttribute(paramDecl, false, paramInfo.TypeInformation);
					methodInfo.Parameters.push_back(paramInfo);
				}

				if(invalidParam)
					continue;

				CommentParser::EnsureValidParameterReferenceComments(methodInfo.Parameters, methodInfo.Documentation);
				outClassInfo.Constructors.push_back(methodInfo);
			}
		}

		for(auto I = curDecl->method_begin(); I != curDecl->method_end(); ++I)
		{
			CXXMethodDecl* methodDecl = *I;

			CXXConstructorDecl* ctorDecl = dyn_cast<CXXConstructorDecl>(methodDecl);
			if(ctorDecl != nullptr)
				continue;

			if(!methodDecl->isUserProvided() || methodDecl->isImplicit())
				continue;

			AnnotateAttr* methodAttr = methodDecl->getAttr<AnnotateAttr>();
			if(methodAttr == nullptr)
				continue;

			StringRef sourceMethodName = methodDecl->getName();

			ScriptExportInformation parsedMethodInfo;
			if(!ScriptExportAttributeParser::ParseExportAttribute(methodDecl, sourceMethodName, parsedMethodInfo))
				continue;

			if(methodDecl->getAccess() != AS_public)
				outs() << "Error: Exported method \"" + sourceMethodName + "\" isn't public. This will likely result in invalid code generation.";

			int methodFlags = 0;

			bool isExternal = false;
			if((parsedMethodInfo.ExportFlags & (int)ExportFlags::ExternalMethod) != 0)
			{
				methodFlags |= (int)MethodFlags::External;
				isExternal = true;
			}

			if((parsedMethodInfo.ExportFlags & (int)ExportFlags::ExternalConstructor) != 0)
			{
				methodFlags |= (int)MethodFlags::External;
				methodFlags |= (int)MethodFlags::Constructor;

				isExternal = true;
			}

			if((parsedMethodInfo.ExportFlags & (int)ExportFlags::InteropOnly))
				methodFlags |= (int)MethodFlags::InteropOnly;

			bool isStatic = false;
			if(methodDecl->isStatic() && !isExternal) // Note: Perhaps add a way to mark external methods as static
			{
				methodFlags |= (int)MethodFlags::Static;
				isStatic = true;
			}

			if((parsedMethodInfo.ExportFlags & (int)ExportFlags::PropertyGetter) != 0)
				methodFlags |= (int)MethodFlags::PropertyGetter;
			else if((parsedMethodInfo.ExportFlags & (int)ExportFlags::PropertySetter) != 0)
				methodFlags |= (int)MethodFlags::PropertySetter;

			MethodInfo methodInfo;
			methodInfo.NativeName = sourceMethodName.str();
			methodInfo.ScriptName = parsedMethodInfo.ExportedTypeName;
			methodInfo.MethodFlags = methodFlags;
			methodInfo.ExternalClass = sourceClassName;
			methodInfo.Visibility = parsedMethodInfo.Visibility;
			methodInfo.API = ParserUtility::ParseAPIFromExportFlags(parsedMethodInfo.ExportFlags);
			methodInfo.MetaData = parsedMethodInfo.MetaData;
			mCommentParser.ParseComments(methodDecl, methodInfo.Documentation);

			bool isProperty = (parsedMethodInfo.ExportFlags & ((int)ExportFlags::PropertyGetter | (int)ExportFlags::PropertySetter));

			if(!isProperty)
			{
				QualType returnType = methodDecl->getReturnType();
				if(!returnType->isVoidType())
				{
					ReturnInfo returnInfo;
					if(!ParseTypeInformation(returnType, returnInfo.TypeInformation))
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
				if((parsedMethodInfo.ExportFlags & (int)ExportFlags::PropertyGetter) != 0)
				{
					QualType returnType = methodDecl->getReturnType();
					if(returnType->isVoidType())
					{
						outs() << "Error: Unable to create a getter for property because method \"" << sourceMethodName
							   << "\" has no return value.\n";
						continue;
					}

					// Note: I can potentially allow an output parameter instead of a return value
					if(methodDecl->param_size() > 1 || ((!isExternal || isStatic) && methodDecl->param_size() > 0))
					{
						outs() << "Error: Unable to create a getter for property because method \"" << sourceMethodName
							   << "\" has parameters.\n";
						continue;
					}

					if(!ParseTypeInformation(returnType, methodInfo.ReturnValue.TypeInformation))
					{
						outs() << "Error: Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.\n";
						continue;
					}

					ParseParameterOrFieldAttribute(methodDecl, false, methodInfo.ReturnValue.TypeInformation);
				}
				else // Must be setter
				{
					QualType returnType = methodDecl->getReturnType();
					if(!returnType->isVoidType())
					{
						outs() << "Error: Unable to create a setter for property because method \"" << sourceMethodName
							   << "\" has a return value.\n";
						continue;
					}

					if(methodDecl->param_size() == 0 || methodDecl->param_size() > 2 || ((!isExternal || isStatic) && methodDecl->param_size() != 1))
					{
						outs() << "Error: Unable to create a setter for property because method \"" << sourceMethodName
							   << "\" has more or less than one parameter.\n";
						continue;
					}

					ParmVarDecl* paramDecl = methodDecl->getParamDecl(isExternal ? 1 : 0);

					VariableInformation paramInfo;
					paramInfo.Name = paramDecl->getName().str();

					if(!ParseTypeInformation(paramDecl->getType(), paramInfo.TypeInformation))
					{
						outs() << "Error: Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.\n";
						continue;
					}
				}
			}

			bool invalidParam = false;
			bool skippedDefaultArg = false;
			for(auto J = methodDecl->param_begin(); J != methodDecl->param_end(); ++J)
			{
				ParmVarDecl* paramDecl = *J;
				QualType paramType = paramDecl->getType();

				VariableInformation parameterInformation;
				parameterInformation.Name = paramDecl->getName().str();

				if(!ParseTypeInformation(paramType, parameterInformation.TypeInformation))
				{
					outs() << "Error: Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.\n";
					invalidParam = true;
					continue;
				}

				if(paramDecl->hasDefaultArg() && !skippedDefaultArg)
				{
					Expr* defaultArg;
					if(paramDecl->hasUninstantiatedDefaultArg())
						defaultArg = paramDecl->getUninstantiatedDefaultArg();
					else
						defaultArg = paramDecl->getDefaultArg();

					if(!TryEvaluateExpression(defaultArg, parameterInformation.DefaultValue, parameterInformation.DefaultValueType))
					{
						outs() << "Error: Method parameter \"" << paramDecl->getName().str() << "\" has a default "
							   << "argument that cannot be constantly evaluated, ignoring it.\n";
						skippedDefaultArg = true;
					}
				}

				ParseParameterOrFieldAttribute(paramDecl, false, parameterInformation.TypeInformation);
				methodInfo.Parameters.push_back(parameterInformation);
			}

			if(invalidParam)
				continue;

			CommentParser::EnsureValidParameterReferenceComments(methodInfo.Parameters, methodInfo.Documentation);

			if(isExternal)
			{
				if(parsedMethodInfo.ExtensionOfType == "T")
					parsedMethodInfo.ExtensionOfType = sourceClassName;

				TypeLookup::RegisterExternalMethod(parsedMethodInfo.ExtensionOfType, methodInfo);
			}
			else
				outClassInfo.Methods.push_back(methodInfo);
		}

		// Look for exported fields & events
		for(auto I = curDecl->field_begin(); I != curDecl->field_end(); ++I)
		{
			FieldDecl* fieldDecl = *I;

			MethodInfo eventInfo;
			if(TryParseEvent(fieldDecl, sourceClassName, eventInfo))
				outClassInfo.Events.push_back(eventInfo);
			else
			{
				FieldInfo fieldInfo;
				fieldInfo.Name = fieldDecl->getName().str();

				ScriptExportInformation parsedFieldInfo;
				bool foundExportAttrib = false;
				for(const auto& entry : fieldDecl->specific_attrs<AnnotateAttr>())
				{
					if(ScriptExportAttributeParser::IsExportAttribute(entry))
					{
						if(ScriptExportAttributeParser::ParseExportAttribute(entry, fieldInfo.Name, parsedFieldInfo))
							foundExportAttrib = true;

						break;
					}
				}

				if(!foundExportAttrib)
					continue;

				std::string typeName;
				if(!ParseTypeInformation(fieldDecl->getType(), fieldInfo.TypeInformation))
				{
					outs() << "Error: Unable to detect type for field \"" << fieldDecl->getName().str() << "\" in \""
						   << sourceClassName << "\". Skipping field.\n";
					continue;
				}

				if(fieldDecl->getAccess() != AS_public)
					outs() << "Error: Exported field \"" + fieldInfo.Name + "\" isn't public. This will likely result in invalid code generation.";

				fieldInfo.MetaData = parsedFieldInfo.MetaData;

				mCommentParser.ParseComments(fieldDecl, fieldInfo.Documentation);
				CommentParser::ClearParameterReferenceComments(fieldInfo.Documentation);

				outClassInfo.Fields.push_back(fieldInfo);

				// Register wrapper methods, this way we can re-use much of the same logic for method/property generation
				MethodInfo getterInfo;
				getterInfo.NativeName = "Get" + fieldInfo.Name;
				getterInfo.ScriptName = parsedFieldInfo.ExportedTypeName;
				getterInfo.Visibility = parsedFieldInfo.Visibility;
				getterInfo.API = ParserUtility::ParseAPIFromExportFlags(parsedFieldInfo.ExportFlags);
				getterInfo.MethodFlags = (int)MethodFlags::PropertyGetter | (int)MethodFlags::FieldWrapper;
				getterInfo.MetaData = fieldInfo.MetaData;

				getterInfo.ReturnValue.TypeInformation = fieldInfo.TypeInformation;
				ParseParameterOrFieldAttribute(fieldDecl, true, getterInfo.ReturnValue.TypeInformation);

				if((parsedFieldInfo.ExportFlags & (int)ExportFlags::InteropOnly) != 0)
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
				setterInfo.MetaData = fieldInfo.MetaData;

				if((parsedFieldInfo.ExportFlags & (int)ExportFlags::InteropOnly) != 0)
					setterInfo.MethodFlags |= (int)MethodFlags::InteropOnly;

				outClassInfo.Methods.push_back(getterInfo);
				outClassInfo.Methods.push_back(setterInfo);
			}
		}

		// Find static data events
		const DeclContext* context = dyn_cast<DeclContext>(curDecl);
		for(auto I = context->decls_begin(); I != context->decls_end(); ++I)
		{
			if(VarDecl* varDecl = dyn_cast<VarDecl>(*I))
			{
				if(!varDecl->isStaticDataMember())
					continue;

				MethodInfo eventInfo;
				if(!TryParseEvent(varDecl, sourceClassName, eventInfo))
					continue;

				eventInfo.MethodFlags |= (int)MethodFlags::Static;
				outClassInfo.Events.push_back(eventInfo);
			}
		}

		auto iter = curDecl->bases_begin();
		while(iter != curDecl->bases_end())
		{
			const CXXBaseSpecifier* baseSpec = iter;
			CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

			// Base classes never need to be exported. Exportable classes will handle their own methods/fields.
			if(ParserUtility::IsBuiltinBaseType(baseDecl) || ScriptExportAttributeParser::IsExportable(baseDecl))
			{
				iter++;
				continue;
			}

			todo.push(baseDecl);
			iter++;
		}
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

	if (!ScriptExportAttributeParser::ParseExportAttribute(attr, sourceClassName, parsedEnumInfo))
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

	ParseNamespace(decl, enumEntry.Namespace);

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
			ScriptExportAttributeParser::ParseExportAttribute(enumAttr, entryName, parsedEnumEntryInfo);

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

bool BansheeCodeGeneratorASTVisitor::VisitCXXRecordDecl(CXXRecordDecl* declaration)
{
	mCommentParser.ParseAndRegisterAllComments(declaration);

	AnnotateAttr* annotateAttribute = declaration->getAttr<AnnotateAttr>();
	if (annotateAttribute == nullptr)
		return true;

	StringRef declarationName = declaration->getName();

	ScriptExportInformation scriptExportInformation;
	scriptExportInformation.ExportedTypeName = declarationName.str();

	if (!ScriptExportAttributeParser::ParseExportAttribute(annotateAttribute, declarationName, scriptExportInformation))
		return true;

	if ((scriptExportInformation.ExportFlags & (int)ExportFlags::ExportAsStruct) != 0)
	{
		StructInfo structInfo;
		if(!TryParseDeclarationAsStruct(declaration, scriptExportInformation, structInfo))
			return true; // Already parsed

		std::string declarationFile = astContext->getSourceManager().getFilename(declaration->getSourceRange().getBegin()).str();
		TypeLookup::RegisterNativeToScriptTypeMapping(structInfo.Namespace, structInfo.NativeName, declarationFile, scriptExportInformation.ExportedTypeName, scriptExportInformation.ExportedFileName, structInfo.API, ExportedClassTypeCategory::Struct);
		TypeLookup::RegisterEntryToGenerate(scriptExportInformation.ExportedFileName, structInfo);
	}
	else
	{
		ClassInfo classInfo;
		if(!TryParseDeclarationAsClass(declaration, scriptExportInformation, classInfo))
			return true; // Already parsed

		// External classes are just containers for external methods, we don't need to process them directly
		if ((scriptExportInformation.ExportFlags & (int)ExportFlags::ExternalMethod) == 0)
		{
			TypeLookup::RegisterEntryToGenerate(scriptExportInformation.ExportedFileName, classInfo);
		}
	}

	return true;
}
