#include "B3DParserUtility.h"
#include "B3DGeneratorUtility.h"
#include "B3DCommentParser.h"
#include "B3DCommon.h"
#include "B3DTypeLookup.h"
#include "B3DXMLCommentGenerator.h"
#include "B3DXMLMappingGenerator.h"

/**
 * Returns a default value that can be used for initializing the variable, field or parameter of the provided type.
 *
 * @param	typeInformation				Information about the native type to generate the default value for.
 * @param	typeMappingInformation		Mapping of the provided type in script.
 */
static std::string GetDefaultValueForType(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	if(typeInformation.IsArrayOrVector())
		return "null";

	if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Primitive)
		return "0";
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Enum)
		return "(" + typeMappingInformation.ScriptTypeName + ")0";
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct)
		return typeMappingInformation.ScriptTypeName + ".Default()";
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::String || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::WString || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::ConstCharString || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Path)
		return "\"\"";
	else // Some class type
		return "null";

	assert(false);
	return ""; // Shouldn't be reached
}

/**
 * Returns a qualified type name in C#, for use in parameters or return values, representing the type in @p typeInformation.
 *
 * @param	typeInformation					Information about the native type to generate the type name for.
 * @param	typeMappingInformation			Mapping of the provided type in script.
 * @param	useOutputParameterPrefix		If true, output parameters will have the 'out' prefix.
 * @param	forceStructAsReference			If true, 'struct' types will always be passed by 'ref'.
 */
static std::string GetScriptQualifiedType(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, bool useOutputParameterPrefix = false, bool forceStructAsReference = false)
{
	std::stringstream output;

	if (useOutputParameterPrefix && typeInformation.IsOutputParameter(typeMappingInformation))
		output << "out ";
	else if (forceStructAsReference && (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && !typeInformation.IsArrayOrVector()))
		output << "ref ";

	output << typeMappingInformation.ScriptTypeName;

	if (typeInformation.IsArrayOrVector())
		output << "[]";

	return output.str();
}

/** Checks if the provided class has any constructors without any parameters. */
static bool HasParameterlessConstructor(const ClassInfo& classInfo)
{
	// Check normal constructors
	for (auto& entry : classInfo.Constructors)
	{
		if (entry.Parameters.size() == 0)
			return true;
	}

	// Check external constructors
	for (auto& entry : classInfo.Methods)
	{
		bool isConstructor = entry.IsFlagSet(MethodFlags::Constructor);
		if (!isConstructor)
			continue;

		if (entry.Parameters.size() == 0)
			return true;
	}

	return false;
}

/** Generates a check for a preprocessor conditional depending on the API the code is currently being compiled for. */
static std::string GenerateAPICheckBegin(ApiFlags api)
{
	if(api == ApiFlags::Framework)
		return "#if !IS_B3D\n";
	else if(api == ApiFlags::Engine)
		return "#if IS_B3D\n";

	return "";
}

/** Ends the preprocessor conditional started by GenerateAPICheckBegin(). These calls must match 1:1. */
static std::string GenerateApiCheckEnd(ApiFlags api)
{
	if(api == ApiFlags::Framework || api == ApiFlags::Engine)
		return "#endif\n";

	return "";
}

/** Returns true if the type is a struct and should be passed as a reference. */
static bool IsStructReference(const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation)
{
	return typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && !typeInformation.IsArrayOrVector();
}

/** Returns true if the provided type is represented as a value type in C#. */
static bool IsInternalMethodParameterValueType(const VariableTypeInformation& typeInformation)
{
	// Note: Purposely not checking for references here, as in C++ they are used to pass data by value
	if (typeInformation.IsQualifierFlagSet(VariableQualifierFlags::IsPointer))
		return false;

	switch(typeInformation.TypeCategory)
	{
	case VariableTypeCategory::SharedPointer:
	case VariableTypeCategory::ResourceHandle: 
	case VariableTypeCategory::GameObjectHandle:
		return false;
	case VariableTypeCategory::Vector:
	case VariableTypeCategory::TInlineArray:
	case VariableTypeCategory::TArray:
	case VariableTypeCategory::Array:
		return IsInternalMethodParameterValueType(typeInformation.AssertGetUnderlyingType());
	default: 
		return true;
	}
}

/**
 * Generates C# attributes that represent property or field meta-data.
 *
 * @param	metaData					Information about the metaData attributes to generate.
 * @param	typeInformation				Native type we're generating the attributes for.
 * @param	typeMappingInformation		Mapping of the provided type in script.
 * @param	isGeneratingStructFields	True if we're generating struct fields, false if generating properties.
 */
static std::string GenerateCSharpMetaDataAttributes(const MemberMetaData& metaData, const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, bool isGeneratingStructFields)
{
	std::stringstream output;

	if(((metaData.Flags & (int)MetaDataFlags::ShowAsLayerMask) != 0) && typeMappingInformation.IsInt64())
		output << "\t\t[LayerMask]\n";

	if ((metaData.Flags & (int)MetaDataFlags::Step) != 0)
		output << "\t\t[Step(" << metaData.IncrementStep << "f)]\n";

	if ((metaData.Flags & (int)MetaDataFlags::Range) != 0)
	{
		std::string isSlider = ((metaData.Flags & (int)MetaDataFlags::ShowAsSlider) != 0) ? "true" : "false";
		output << "\t\t[Range(" << metaData.RangeMinimum << "f, " << metaData.RangeMaximum << "f, " << isSlider << ")]\n";
	}
	else if ((metaData.Flags & (int)MetaDataFlags::ShowAsSlider) != 0)
		output << "\t\t[Range(float.MinValue, float.MaxValue, true)]\n";

	if(((metaData.Flags & (int)MetaDataFlags::Order) != 0))
		output << "\t\t[Order(" << metaData.UIOrder << ")]\n";

	if(((metaData.Flags & (int)MetaDataFlags::Category) != 0))
		output << "\t\t[Category(\"" << metaData.UICategory << "\")]\n";

	if(((metaData.Flags & (int)MetaDataFlags::Inline) != 0))
		output << "\t\t[Inline]\n";

	bool notNull = (metaData.Flags & (int)MetaDataFlags::NotNull) != 0;
	bool passByCopy = (metaData.Flags & (int)MetaDataFlags::PassByCopy) != 0;

	const bool isPassedByValue = IsInternalMethodParameterValueType(typeInformation);
	if(!isGeneratingStructFields && (typeMappingInformation.IsClassType() && isPassedByValue))
	{
		notNull = true;
		passByCopy = true;
	}

	if(notNull)
		output << "\t\t[NotNull]\n";

	if(passByCopy)
		output << "\t\t[PassByCopy]\n";

	if(((metaData.Flags & (int)MetaDataFlags::ApplyOnDirty) != 0))
		output << "\t\t[ApplyOnDirty]\n";

	if(((metaData.Flags & (int)MetaDataFlags::AsQuaternion) != 0))
		output << "\t\t[AsQuaternion]\n";

	if(((metaData.Flags & (int)MetaDataFlags::LoadOnAssign) != 0))
		output << "\t\t[LoadOnAssign]\n";

	if(((metaData.Flags & (int)MetaDataFlags::HDR) != 0))
		output << "\t\t[HDR]\n";

	return output.str();
}

/**
 * Generates a default value to assign to a variable, field or parameter.
 *
 * @param variableInformation			Information about the variable, field or parameter to assign the value to.
 * @return								String containing the value to assign, to be placed after the '=' operator.
 */
static std::string GenerateCSharpDefaultValueAssignment(const VariableInformation& variableInformation)
{
	if (variableInformation.DefaultValueType.empty() || variableInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags)
		return variableInformation.DefaultValue;
	else
	{
		// Constructor or cast, assuming constructor as cast implies a constructor accepting the type exists (and we don't export cast operators anyway)
		TypeMappingInformation defaultValueTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(variableInformation.DefaultValueType);

		if(defaultValueTypeMappingInformation.TypeCategory == ExportedClassTypeCategory::Struct && variableInformation.DefaultValue.empty())
			return defaultValueTypeMappingInformation.ScriptTypeName + ".Default()";
		else
			return "new " + defaultValueTypeMappingInformation.ScriptTypeName + "(" + variableInformation.DefaultValue + ")";
	}
}

/**
 * Generates parameters to use when constructing a C# method signature.
 *
 * @param methodInfo			Structure describing the method to generate parameters for.
 * @param forInternalMethod		True if the parameters are generated for an Internal_ method call, or false if for a regular method call.
 * @return						String containing a comma (,) separate list of parameters.
 */
static std::string GenerateCSharpMethodParameters(const MethodInfo& methodInfo, bool forInternalMethod)
{
	std::stringstream output;
	for (auto I = methodInfo.Parameters.begin(); I != methodInfo.Parameters.end(); ++I)
	{
		const VariableInformation& paramInfo = *I;

		if(!forInternalMethod && !paramInfo.DefaultValueType.empty() && paramInfo.TypeInformation.TypeCategory != VariableTypeCategory::Flags)
		{
			// We don't generate parameters that have complex default values (as they're not supported in C#).
			// Instead the post-processor has generated different versions of this method, so we can just skip
			// such parameters
			continue;
		}

		if (I != methodInfo.Parameters.begin())
			output << ", ";

		const TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(paramInfo.TypeInformation);
		const std::string qualifiedType = GetScriptQualifiedType(paramInfo.TypeInformation, parameterTypeMappingInformation, true, forInternalMethod);

		bool isLastParameter = (I + 1) == methodInfo.Parameters.end();
		if (paramInfo.TypeInformation.IsParameterFlagSet(ParameterFlags::VarParams) && isLastParameter)
			output << "params ";

		output << qualifiedType << " " << paramInfo.Name;

		if (!forInternalMethod && !paramInfo.DefaultValue.empty())
			output << " = " << GenerateCSharpDefaultValueAssignment(paramInfo);
	}

	return output.str();
}

/**
 * Generates arguments to use when calling a C# method.
 *
 * @param methodInfo			Structure describing the method to call.
 * @param forInternalMethod		True if the arguments are generated for an Internal_ method call, or false if for a regular method call.
 * @return						String containing a comma (,) separated list of arguments.
 */
static std::string GenerateCSharpMethodArguments(const MethodInfo& methodInfo, bool forInternalMethod)
{
	std::stringstream output;
	for (auto I = methodInfo.Parameters.begin(); I != methodInfo.Parameters.end(); ++I)
	{
		const VariableInformation& parameterInformation = *I;
		const TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);

		if (parameterInformation.TypeInformation.IsOutputParameter(parameterTypeMappingInformation))
			output << "out ";
		else if (forInternalMethod && IsStructReference(parameterInformation.TypeInformation, parameterTypeMappingInformation))
			output << "ref ";

		output << parameterInformation.Name;

		if ((I + 1) != methodInfo.Parameters.end())
			output << ", ";
	}

	return output.str();
}

/**
 * Generates variable containing default values, for types that cannot have their default values specified in the parameter list directly.
 *
 * @param methodInfo		Structure describing the method that will be called with the arguments.
 * @param indent			Whitespace to insert before the generated lines of code.
 * @return					Code creating local variables (using parameter names) initialized for default values, for types that need it.
 */
static std::string GenerateCSharpMethodDefaultArgumentAssignments(const MethodInfo& methodInfo, const std::string& indent)
{
	std::stringstream output;
	for (auto I = methodInfo.Parameters.begin(); I != methodInfo.Parameters.end(); ++I)
	{
		const VariableInformation& parameterInformation = *I;

		if (parameterInformation.DefaultValueType.empty() || parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags)
			continue;

		if (parameterInformation.DefaultValueType == "null" || parameterInformation.DefaultValue == "null")
		{
			TypeMappingInformation paramTypeInfo = TypeLookup::GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);
			output << indent << paramTypeInfo.ScriptTypeName << " " << parameterInformation.Name << " = " << parameterInformation.DefaultValue << ";\n";
		}
		else
		{
			TypeMappingInformation defaultValTypeInfo = TypeLookup::GetNativeToScriptTypeMapping(parameterInformation.DefaultValueType);
			output << indent << defaultValTypeInfo.ScriptTypeName << " " << parameterInformation.Name << " = ";
			output << "new " << defaultValTypeInfo.ScriptTypeName << "(" << parameterInformation.DefaultValue << ");\n";
		}
	}

	return output.str();
}

/**
 * Generates a parameter list for an event signature.
 *
 * @param methodInfo		Information about the event to generate the parameters for.
 * @return					String containing a comma (,) separated list of parameters.
 */
static std::string GenerateCSharpEventSignature(const MethodInfo& methodInfo)
{
	std::stringstream output;
	for (auto I = methodInfo.Parameters.begin(); I != methodInfo.Parameters.end(); ++I)
	{
		const VariableInformation& paramInfo = *I;
		TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(paramInfo.TypeInformation);
		std::string type = GetScriptQualifiedType(paramInfo.TypeInformation, parameterTypeMappingInformation);

		output << type;

		if ((I + 1) != methodInfo.Parameters.end())
			output << ", ";
	}

	return output.str();
}

/**
 * Generates a list of arguments used for calling an event.
 *
 * @param methodInfo		Information about the event to generate the arguments for.
 * @return					String containing a comma (,) separated list of arguments.
 */
static std::string GenerateCSharpEventArguments(const MethodInfo& methodInfo)
{
	std::stringstream output;

	for (auto I = methodInfo.Parameters.begin(); I != methodInfo.Parameters.end(); ++I)
	{
		output << I->Name;

		if ((I + 1) != methodInfo.Parameters.end())
			output << ", ";
	}

	return output.str();
}

/**
 * Generates the full method signature for an 'Internal' method.
 *
 * @param classInformation			Information about the class we're generating the method for.
 * @param methodInformation			Information about the method to generate.
 * @param typeMappingInformation	Information about the mapping of the native type to script type.
 * @return							Signature of the method, with return value, method name and parameter list.
 */
static std::string GenerateCSharpInternalMethodSignature(const ClassInfo& classInformation, const MethodInfo& methodInformation, const TypeMappingInformation& typeMappingInformation)
{
	const bool classHasGlobalSingleInstance = classInformation.HasGlobalSingleInstance();
	const bool isStatic = methodInformation.IsFlagSet(MethodFlags::Static);
	const bool isCtor = methodInformation.IsFlagSet(MethodFlags::Constructor);

	std::stringstream output;

	bool returnAsParameter = false;
	if (methodInformation.ReturnValue.TypeInformation.IsEmpty() || isCtor)
		output << "void";
	else
	{
		const TypeMappingInformation returnTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(methodInformation.ReturnValue.TypeInformation);
		if (!GeneratorUtility::CanBeReturned(methodInformation.ReturnValue.TypeInformation, returnTypeMappingInformation))
		{
			output << "void";
			returnAsParameter = true;
		}
		else
		{
			const std::string qualifiedType = GetScriptQualifiedType(methodInformation.ReturnValue.TypeInformation, returnTypeMappingInformation);
			output << qualifiedType;
		}
	}

	output << " ";

	output << "Internal_" << methodInformation.InteropName << "(";

	if (isCtor)
	{
		output << typeMappingInformation.ScriptTypeName << " managedInstance";

		if (methodInformation.Parameters.size() > 0)
			output << ", ";
	}
	else if (!isStatic && !classHasGlobalSingleInstance)
	{
		output << "IntPtr thisPtr";

		if (methodInformation.Parameters.size() > 0 || returnAsParameter)
			output << ", ";
	}

	output << GenerateCSharpMethodParameters(methodInformation, true);

	if (returnAsParameter)
	{
		const TypeMappingInformation returnTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(methodInformation.ReturnValue.TypeInformation);
		const std::string qualifiedType = GetScriptQualifiedType(methodInformation.ReturnValue.TypeInformation, returnTypeMappingInformation);

		if (methodInformation.Parameters.size() > 0)
			output << ", ";

		output << "out " << qualifiedType << " __output";
	}

	output << ")";
	return output.str();
}

/** Generates a full declaration of a C# class. */
static std::string GenerateCSharpClass(const ClassInfo& classInformation)
{
	const TypeMappingInformation& typeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(classInformation.NativeName);
	const bool classHasGlobalSingleInstance = classInformation.HasGlobalSingleInstance();

	std::stringstream ctors;
	std::stringstream properties;
	std::stringstream events;
	std::stringstream methods;
	std::stringstream interops;

	// Private constructor for runtime use
	MethodInfo privateConstructorInformation = classInformation.FindUnusedConstructorSignature();
	ctors << "\t\tprivate " << typeMappingInformation.ScriptTypeName << "(" << GenerateCSharpMethodParameters(privateConstructorInformation, false) << ") { }" << std::endl;

	// Parameterless constructor in case anything derives from this class
	if (!HasParameterlessConstructor(classInformation))
		ctors << "\t\tprotected " << typeMappingInformation.ScriptTypeName << "() { }" << std::endl;

	ctors << std::endl;

	// Constructors
	for (auto& entry : classInformation.Constructors)
	{
		if (!entry.IsFlagSet(MethodFlags::CSOnly))
		{
			// Generate interop
			interops << GenerateAPICheckBegin(entry.API);
			interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]" << std::endl;
			interops << "\t\tprivate static extern void Internal_" << entry.InteropName << "(" << typeMappingInformation.ScriptTypeName << " managedInstance";

			if (entry.Parameters.size() > 0)
				interops << ", " << GenerateCSharpMethodParameters(entry, true);

			interops << ");\n";
			interops << GenerateApiCheckEnd(entry.API);
		}

		bool interopOnly = entry.IsFlagSet(MethodFlags::InteropOnly);
		if (interopOnly)
			continue;

		ctors << GenerateAPICheckBegin(entry.API);
		ctors << XMLCommentGenerator::GenerateXMLComment(entry.Documentation, "\t\t");

		if (entry.Visibility == CSVisibility::Internal)
			ctors << "\t\tinternal ";
		else if (entry.Visibility == CSVisibility::Private)
			ctors << "\t\tprivate ";
		else
			ctors << "\t\tpublic ";

		ctors << typeMappingInformation.ScriptTypeName << "(" << GenerateCSharpMethodParameters(entry, false) << ")" << std::endl;
		ctors << "\t\t{" << std::endl;
		ctors << GenerateCSharpMethodDefaultArgumentAssignments(entry, "\t\t\t");
		ctors << "\t\t\tInternal_" << entry.InteropName << "(this";

		if (entry.Parameters.size() > 0)
			ctors << ", " << GenerateCSharpMethodArguments(entry, true);

		ctors << ");" << std::endl;
		ctors << "\t\t}" << std::endl;
		ctors << GenerateApiCheckEnd(entry.API);
		ctors << std::endl;
	}

	// 'Ref' property & conversion operator to RRef<T>
	if(typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
	{
		interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]\n";
		interops << "\t\tprivate static extern RRef<" << typeMappingInformation.ScriptTypeName << "> Internal_GetRef(IntPtr thisPtr);\n";

		properties << "\t\t/// <summary>Returns a reference wrapper for this resource.</summary>\n";
		properties << "\t\tpublic RRef<" << typeMappingInformation.ScriptTypeName << "> Ref\n";
		properties << "\t\t{\n";
		properties << "\t\t\tget { return Internal_GetRef(mCachedPtr); }\n";
		properties << "\t\t}\n";
		properties << "\n";

		methods << "\t\t/// <summary>Returns a reference wrapper for this resource.</summary>\n";
		methods << "\t\tpublic static implicit operator RRef<" << typeMappingInformation.ScriptTypeName << ">(" << typeMappingInformation.ScriptTypeName << " x)\n";
		methods << "\t\t{\n";
		methods << "\t\t\tif(x != null)\n";
		methods << "\t\t\t\treturn Internal_GetRef(x.mCachedPtr);\n";
		methods << "\t\t\telse\n";
		methods << "\t\t\t\treturn null;\n";
		methods << "\t\t}\n\n";
	}

	// External constructors, methods and interop stubs
	for (auto& entry : classInformation.Methods)
	{
		// Generate interop
		if (!entry.IsFlagSet(MethodFlags::CSOnly))
		{
			interops << GenerateAPICheckBegin(entry.API);
			interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]" << std::endl;
			interops << "\t\tprivate static extern " << GenerateCSharpInternalMethodSignature(classInformation, entry, typeMappingInformation) << ";";
			interops << std::endl;
			interops << GenerateApiCheckEnd(entry.API);
		}

		bool interopOnly = entry.IsFlagSet(MethodFlags::InteropOnly);
		if (interopOnly)
			continue;

		bool isConstructor = entry.IsFlagSet(MethodFlags::Constructor);
		bool isStatic = entry.IsFlagSet(MethodFlags::Static);

		if (isConstructor)
		{
			ctors << GenerateAPICheckBegin(entry.API);
			ctors << XMLCommentGenerator::GenerateXMLComment(entry.Documentation, "\t\t");

			if (entry.Visibility == CSVisibility::Internal)
				ctors << "\t\tinternal ";
			else if (entry.Visibility == CSVisibility::Private)
				ctors << "\t\tprivate ";
			else
				ctors << "\t\tpublic ";

			ctors << typeMappingInformation.ScriptTypeName << "(" << GenerateCSharpMethodParameters(entry, false) << ")" << std::endl;
			ctors << "\t\t{" << std::endl;
			ctors << GenerateCSharpMethodDefaultArgumentAssignments(entry, "\t\t\t");
			ctors << "\t\t\tInternal_" << entry.InteropName << "(this";

			if (entry.Parameters.size() > 0)
				ctors << ", " << GenerateCSharpMethodArguments(entry, true);

			ctors << ");" << std::endl;
			ctors << "\t\t}" << std::endl;
			ctors << GenerateApiCheckEnd(entry.API);
			ctors << std::endl;
		}
		else
		{
			const bool isProperty = entry.IsFlagSet(MethodFlags::PropertyGetter) || entry.IsFlagSet(MethodFlags::PropertySetter);
			if (!isProperty)
			{
				TypeMappingInformation returnTypeMappingInformation;
				std::string returnType;
				if (entry.ReturnValue.TypeInformation.IsEmpty())
					returnType = "void";
				else
				{
					returnTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(entry.ReturnValue.TypeInformation);
					returnType = GetScriptQualifiedType(entry.ReturnValue.TypeInformation, returnTypeMappingInformation);
				}

				methods << GenerateAPICheckBegin(entry.API);
				methods << XMLCommentGenerator::GenerateXMLComment(entry.Documentation, "\t\t");

				if (entry.Visibility == CSVisibility::Internal)
					methods << "\t\tinternal ";
				else if (entry.Visibility == CSVisibility::Private)
					methods << "\t\tprivate ";
				else
					methods << "\t\tpublic ";

				if (isStatic || classHasGlobalSingleInstance)
					methods << "static ";

				methods << returnType << " " << entry.ScriptName << "(" << GenerateCSharpMethodParameters(entry, false) << ")" << std::endl;
				methods << "\t\t{" << std::endl;
				methods << GenerateCSharpMethodDefaultArgumentAssignments(entry, "\t\t\t");

				bool returnByParam = false;
				if (!entry.ReturnValue.TypeInformation.IsEmpty())
				{
					if (!GeneratorUtility::CanBeReturned(entry.ReturnValue.TypeInformation, returnTypeMappingInformation))
					{
						methods << "\t\t\t" << returnType << " temp;" << std::endl;
						methods << "\t\t\tInternal_" << entry.InteropName << "(";
						returnByParam = true;
					}
					else
						methods << "\t\t\treturn Internal_" << entry.InteropName << "(";
				}
				else
					methods << "\t\t\tInternal_" << entry.InteropName << "(";

				if (!isStatic && !classHasGlobalSingleInstance)
				{
					methods << "mCachedPtr";

					if (entry.Parameters.size() > 0 || returnByParam)
						methods << ", ";
				}

				methods << GenerateCSharpMethodArguments(entry, true);

				if (returnByParam)
				{
					if (entry.Parameters.size() > 0)
						methods << ", ";

					methods << "out temp";
				}

				methods << ");" << std::endl;

				if (returnByParam)
					methods << "\t\t\treturn temp;" << std::endl;

				methods << "\t\t}" << std::endl;
				methods << GenerateApiCheckEnd(entry.API);
				methods << std::endl;
			}
		}
	}

	// Properties
	for (auto& entry : classInformation.Properties)
	{
		const TypeMappingInformation propertyTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(entry.TypeInformation);
		const std::string propertyQualifiedTypeName = GetScriptQualifiedType(entry.TypeInformation, propertyTypeMappingInformation);

		properties << GenerateAPICheckBegin(entry.API);
		properties << XMLCommentGenerator::GenerateXMLComment(entry.Documentation, "\t\t");

		bool defaultVisible = entry.Visibility != CSVisibility::Internal && entry.Visibility != CSVisibility::Private &&
			!entry.SetterName.empty();
		if (defaultVisible)
		{
			if ((entry.MetaData.Flags & (int)MetaDataFlags::ForceHideInInspector) == 0)
				properties << "\t\t[ShowInInspector]" << std::endl;
		}
		else
		{
			if ((entry.MetaData.Flags & (int)MetaDataFlags::ForceShowInInspector) != 0)
				properties << "\t\t[ShowInInspector]" << std::endl;
		}

		properties << GenerateCSharpMetaDataAttributes(entry.MetaData, entry.TypeInformation, propertyTypeMappingInformation, false);

		properties << "\t\t[NativeWrapper]\n";

		if (entry.Visibility == CSVisibility::Internal)
			properties << "\t\tinternal ";
		else if (entry.Visibility == CSVisibility::Private)
			properties << "\t\tprivate ";
		else
			properties << "\t\tpublic ";

		if (entry.IsStatic || classHasGlobalSingleInstance)
			properties << "static ";

		properties << propertyQualifiedTypeName << " " << entry.ScriptName << std::endl;
		properties << "\t\t{" << std::endl;

		if (!entry.GetterName.empty())
		{
			if (GeneratorUtility::CanBeReturned(entry.TypeInformation, propertyTypeMappingInformation))
			{
				properties << "\t\t\tget { return Internal_" << entry.GetterName << "(";

				if (!entry.IsStatic && !classHasGlobalSingleInstance)
					properties << "mCachedPtr";

				properties << "); }" << std::endl;
			}
			else
			{
				properties << "\t\t\tget" << std::endl;
				properties << "\t\t\t{" << std::endl;
				properties << "\t\t\t\t" << propertyQualifiedTypeName << " temp;" << std::endl;

				properties << "\t\t\t\tInternal_" << entry.GetterName << "(";

				if (!entry.IsStatic && !classHasGlobalSingleInstance)
					properties << "mCachedPtr, ";

				properties << "out temp);" << std::endl;

				properties << "\t\t\t\treturn temp;" << std::endl;
				properties << "\t\t\t}" << std::endl;
			}
		}

		if (!entry.SetterName.empty())
		{
			properties << "\t\t\tset { Internal_" << entry.SetterName << "(";

			if (!entry.IsStatic && !classHasGlobalSingleInstance)
				properties << "mCachedPtr, ";

			if(IsStructReference(entry.TypeInformation, propertyTypeMappingInformation))
				properties << "ref ";

			properties << "value); }" << std::endl;
		}

		properties << "\t\t}" << std::endl;
		properties << GenerateApiCheckEnd(entry.API);
		properties << std::endl;
	}

	// Events & callbacks
	for(auto& entry : classInformation.Events)
	{
		bool isStatic = entry.IsFlagSet(MethodFlags::Static);
		bool isCallback = entry.IsFlagSet(MethodFlags::Callback);
		bool isInternal = entry.IsFlagSet(MethodFlags::InteropOnly);

		events << GenerateAPICheckBegin(entry.API);
		events << XMLCommentGenerator::GenerateXMLComment(entry.Documentation, "\t\t");
		events << "\t\t";

		if (!isCallback && !isInternal)
		{
			if (entry.Visibility == CSVisibility::Internal)
				events << "internal ";
			else if (entry.Visibility == CSVisibility::Private)
				events << "private ";
			else
				events << "public ";
		}

		if (isStatic || classHasGlobalSingleInstance)
			events << "static ";

		if (!isCallback && !isInternal)
		{
			events << "event Action";

			if (!entry.Parameters.empty())
				events << "<" << GenerateCSharpEventSignature(entry) << ">";

			events << " " << entry.ScriptName << ";\n\n";
		}
		else
		{
			events << "partial void Callback_" << entry.ScriptName << "(";

			if (!entry.Parameters.empty())
				events << GenerateCSharpMethodParameters(entry, false);

			events << ");\n";
			events << GenerateApiCheckEnd(entry.API);
			events << "\n";
		}

		// Event interop
		interops << GenerateAPICheckBegin(entry.API);

		interops << "\t\tprivate ";

		if (isStatic || classHasGlobalSingleInstance)
			interops << "static ";

		interops << "void Internal_" << entry.InteropName << "(" << GenerateCSharpMethodParameters(entry, true) << ")" << std::endl;
		interops << "\t\t{" << std::endl;
		if (!isCallback && !isInternal)
			interops << "\t\t\t" << entry.ScriptName << "?.Invoke(" << GenerateCSharpEventArguments(entry) << ");\n";
		else
			interops << "\t\t\tCallback_" << entry.ScriptName << "(" << GenerateCSharpEventArguments(entry) << ");\n";
		interops << "\t\t}" << std::endl;
		interops << GenerateApiCheckEnd(entry.API);
	}

	std::stringstream output;
	output << GenerateAPICheckBegin(classInformation.API);

	if(!classInformation.DocumentationGroup.empty())
	{
		output << "\t/** @addtogroup " << classInformation.DocumentationGroup << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << XMLCommentGenerator::GenerateXMLComment(classInformation.Documentation, "\t");

	// Force non-resource and non-component types to show in inspector, except explicitly hidden
	if (typeMappingInformation.IsClassType() || (!classInformation.IsFlagSet(ClassFlags::HideInInspector)))
		output << "\t[ShowInInspector]\n";

	if (classInformation.Visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (classInformation.Visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (classInformation.Visibility == CSVisibility::Private)
		output << "\tprivate ";
	else
		output << "\t";

	std::string baseType;
	if (!classInformation.BaseClassName.empty())
	{
		TypeMappingInformation baseTypeInfo = TypeLookup::GetNativeToScriptTypeMapping(classInformation.BaseClassName);
		baseType = baseTypeInfo.ScriptTypeName;
	}
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Resource)
		baseType = "Resource";
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Component)
		baseType = "Component";
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::GUIElement)
		baseType = "GUIElement";
	else
		baseType = "ScriptObject";

	output << "partial class " << typeMappingInformation.ScriptTypeName << " : " << baseType;

	output << std::endl;
	output << "\t{" << std::endl;

	output << ctors.str();
	output << properties.str();
	output << events.str();
	output << methods.str();
	output << interops.str();

	output << "\t}" << std::endl;

	if(!classInformation.DocumentationGroup.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << GenerateApiCheckEnd(classInformation.API);

	return output.str();
}

/** Generates a full declaration of a C# struct. */
static std::string GenerateCSharpStruct(const StructInfo& input)
{
	std::stringstream output;
	output << GenerateAPICheckBegin(input.API);

	if(!input.DocumentationGroup.empty())
	{
		output << "\t/** @addtogroup " << input.DocumentationGroup << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << XMLCommentGenerator::GenerateXMLComment(input.Documentation, "\t");

	output << "\t[StructLayout(LayoutKind.Sequential), SerializeObject]\n";

	if (input.Visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (input.Visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (input.Visibility == CSVisibility::Private)
		output << "\tprivate ";
	else
		output << "\t";

	const TypeMappingInformation& typeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(input.NativeName);
	std::string scriptName;
	if(input.IsFlagSet(StructFlags::IsTemplate))
	{
		std::stringstream scriptNameStream;
		scriptNameStream << input.NativeNameWithoutTemplateArguments << "<";

		for(uint32_t templateParameterIndex = 0; templateParameterIndex < (uint32_t)input.TemplateParameters.size(); ++templateParameterIndex)
		{
			scriptNameStream << input.TemplateParameters[templateParameterIndex].Name;

			if((templateParameterIndex + 1) < (uint32_t)input.TemplateParameters.size())
				scriptNameStream << ", ";
		}

		scriptNameStream << ">";

		scriptName = scriptNameStream.str();
	}
	else
		scriptName = typeMappingInformation.ScriptTypeName;

	output << "partial struct " << scriptName;

	output << std::endl;
	output << "\t{" << std::endl;

	if(!input.IsFlagSet(StructFlags::IsTemplate))
	{
		for (auto& entry : input.Constructors)
		{
			bool isParameterless = entry.Parameters.size() == 0;
			bool isStaticMethod = !entry.StaticMethodName.empty() || isParameterless;
			if(!entry.StaticMethodName.empty())
			{
				output << XMLCommentGenerator::GenerateXMLComment(entry.Documentation, "\t\t");
				output << "\t\tpublic static " << scriptName << " " << entry.StaticMethodName << "(";
			}
			else if (isParameterless) // Parameterless constructors not supported on C# structs
			{
				output << "\t\t/// <summary>Initializes the struct with default values.</summary>" << std::endl;
				output << "\t\tpublic static " << scriptName << " Default(";
			}
			else
			{
				output << XMLCommentGenerator::GenerateXMLComment(entry.Documentation, "\t\t");
				output << "\t\tpublic " << scriptName << "(";
			}

			std::vector<std::string> skippedParameters;
			for (auto I = entry.Parameters.begin(); I != entry.Parameters.end(); ++I)
			{
				const VariableInformation& parameterInformation = *I;
				const TypeMappingInformation parameterTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);

				if (parameterInformation.TypeInformation.IsOutputParameter(parameterTypeMappingInformation))
				{
					// We report the error during field generation, as it checks for the same condition
					continue;
				}

				if(!parameterInformation.DefaultValueType.empty() && parameterInformation.TypeInformation.TypeCategory != VariableTypeCategory::Flags)
				{
					// We don't generate parameters that have complex default values (as they're not supported in C#).
					// Instead the post-processor has generated different versions of this method, so we can just skip
					// such parameters
					skippedParameters.push_back(parameterInformation.Name);
					continue;
				}

				const std::string qualifiedType = GetScriptQualifiedType(parameterInformation.TypeInformation, parameterTypeMappingInformation, true, false);

				bool isLastParameter = (I + 1) == entry.Parameters.end();
				if (parameterInformation.TypeInformation.IsParameterFlagSet(ParameterFlags::VarParams) && isLastParameter)
					output << "params ";

				output << qualifiedType << " " << parameterInformation.Name;

				if (!parameterInformation.DefaultValue.empty())
					output << " = " << GenerateCSharpDefaultValueAssignment(parameterInformation);

				if ((I + 1) != entry.Parameters.end())
					output << ", ";
			}

			output << ")" << std::endl;
			output << "\t\t{" << std::endl;

			std::string thisPtr;
			if (isStaticMethod)
			{
				output << "\t\t\t" << scriptName << " value = new " << scriptName << "();" << std::endl;
				thisPtr = "value";
			}
			else
				thisPtr = "this";

			for (auto I = input.Fields.begin(); I != input.Fields.end(); ++I)
			{
				const VariableInformation& fieldInformation = *I;
				const TypeMappingInformation fieldTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(fieldInformation.TypeInformation);
				if (fieldInformation.TypeInformation.IsOutputParameter(fieldTypeMappingInformation))
				{
					// We report the error during field generation, as it checks for the same condition
					continue;
				}

				std::string fieldName = fieldInformation.Name;

				bool foundFieldAssignment = false;
				auto iterFind = entry.FieldAssignments.find(fieldInformation.Name);
				if (iterFind != entry.FieldAssignments.end())
				{
					const std::string& parameterName = iterFind->second;
					auto itFoundSkippedParameter = std::find(skippedParameters.begin(), skippedParameters.end(), parameterName);
					if(itFoundSkippedParameter == skippedParameters.end())
					{
						output << "\t\t\t" << thisPtr << "." << fieldName << " = " << parameterName << ";" << std::endl;
						foundFieldAssignment = true;
					}
				}

				if(!foundFieldAssignment)
				{
					std::string defaultValue;
					if (!fieldInformation.DefaultValue.empty())
						defaultValue = GenerateCSharpDefaultValueAssignment(fieldInformation);
					else
						defaultValue = GetDefaultValueForType(fieldInformation.TypeInformation, fieldTypeMappingInformation);

					output << "\t\t\t" << thisPtr << "." << fieldName << " = " << defaultValue << ";" << std::endl;
				}
			}

			if (isParameterless)
			{
				output << std::endl;
				output << "\t\t\treturn value;" << std::endl;
			}

			output << "\t\t}" << std::endl;
			output << std::endl;
		}

		if(!input.BaseClassName.empty())
		{
			TypeMappingInformation baseTypeInfo = TypeLookup::GetNativeToScriptTypeMapping(input.BaseClassName);
			StructInfo* baseStructInfo = TypeLookup::FindStructInformation(input.BaseClassName);
			if (baseStructInfo != nullptr)
			{
				// GetBase()
				output << "\t\t///<summary>\n";
				output << "\t\t/// Returns a subset of this struct. This subset usually contains common fields shared with another struct.\n";
				output << "\t\t///</summary>\n";
				output << "\t\tpublic " << baseTypeInfo.ScriptTypeName << " GetBase()\n";
				output << "\t\t{\n";
				output << "\t\t\t" << baseTypeInfo.ScriptTypeName << " value;\n";

				for (auto I = baseStructInfo->Fields.begin(); I != baseStructInfo->Fields.end(); ++I)
				{
					const FieldInfo& fieldInfo = *I;
					output << "\t\t\tvalue." << fieldInfo.Name << " = " << fieldInfo.Name << ";\n";
				}

				output << "\t\t\treturn value;\n";
				output << "\t\t}\n";
				output << "\n";

				// SetBase()
				output << "\t\t///<summary>\n";
				output << "\t\t/// Assigns values to a subset of fields of this struct. This subset usually contains common field shared with \n";
				output << "\t\t/// another struct.\n";
				output << "\t\t///</summary>\n";
				output << "\t\tpublic void SetBase(" << baseTypeInfo.ScriptTypeName << " value)\n";
				output << "\t\t{\n";

				for (auto I = baseStructInfo->Fields.begin(); I != baseStructInfo->Fields.end(); ++I)
				{
					const FieldInfo& fieldInfo = *I;
					output << "\t\t\t" << fieldInfo.Name << " = value." << fieldInfo.Name << ";\n";
				}

				output << "\t\t}\n";
				output << "\n";
			}
		}
	}

	for (auto I = input.Fields.begin(); I != input.Fields.end(); ++I)
	{
		const FieldInfo& fieldInformation = *I;
		const TypeMappingInformation fieldTypeMappingInformation = TypeLookup::GetNativeToScriptTypeMapping(fieldInformation.TypeInformation);

		if (fieldInformation.TypeInformation.IsOutputParameter(fieldTypeMappingInformation))
		{
			outs() << "Error: Invalid field type found in struct \"" << scriptName << "\" for field \"" << fieldInformation.Name << "\". Skipping.\n";
			continue;
		}

		output << XMLCommentGenerator::GenerateXMLComment(fieldInformation.Documentation, "\t\t");
		output << GenerateCSharpMetaDataAttributes(fieldInformation.MetaData, fieldInformation.TypeInformation, fieldTypeMappingInformation, true);

		if ((fieldInformation.MetaData.Flags & (int)MetaDataFlags::ForceHideInInspector) != 0)
			output << "\t\t[HideInInspector]\n";

		output << "\t\tpublic ";

		if(input.IsFlagSet(StructFlags::IsTemplate) && fieldInformation.TemplateParameterIndex != ~0u)
		{
			if(fieldInformation.TemplateParameterIndex < input.TemplateParameters.size())
				output << input.TemplateParameters[fieldInformation.TemplateParameterIndex].Name;
		}
		else
			output << fieldTypeMappingInformation.ScriptTypeName;

		if (fieldInformation.TypeInformation.IsArrayOrVector())
			output << "[]";

		output << " ";
		output << fieldInformation.Name;

		output << ";\n";
	}

	output << "\t}\n";

	if(!input.DocumentationGroup.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << GenerateApiCheckEnd(input.API);
	return output.str();
}

/** Generates a full declaration of a C# enum. */
static std::string GenerateCSharpEnum(const EnumInfo& input)
{
	std::stringstream output;
	output << GenerateAPICheckBegin(input.API);

	if(!input.DocumentationGroup.empty())
	{
		output << "\t/** @addtogroup " << input.DocumentationGroup << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << XMLCommentGenerator::GenerateXMLComment(input.Documentation, "\t");
	if (input.Visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (input.Visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (input.Visibility == CSVisibility::Private)
		output << "\tprivate ";

	output << "enum " << input.ScriptName;

	if (!input.ExplicitUnderlyingCSharpType.empty())
		output << " : " << input.ExplicitUnderlyingCSharpType;

	output << std::endl;
	output << "\t{" << std::endl;

	for (auto I = input.Entries.begin(); I != input.Entries.end(); ++I)
	{
		if (I != input.Entries.begin())
			output << ",\n";

		const EnumEntryInfo& entryInfo = I->second;

		output << XMLCommentGenerator::GenerateXMLComment(entryInfo.Documentation, "\t\t");
		output << "\t\t" << entryInfo.ScriptName;
		output << " = ";
		output << entryInfo.Value;
	}

	output << "\n";
	output << "\t}" << std::endl;

	if(!input.DocumentationGroup.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << GenerateApiCheckEnd(input.API);
	return output.str();
}

void GenerateCSharp(StringRef engineOutputFolder, StringRef editorOutputFolder, bool generateEditorCode)
{
	GeneratorUtility::CleanAndPrepareFolder(engineOutputFolder);

	if (generateEditorCode)
	{
		GeneratorUtility::CleanAndPrepareFolder(editorOutputFolder);
	}

	// Generate CS
	for (auto& fileInfo : TypeLookup::GetFilesToGenerate())
	{
		if (fileInfo.second.InEditor && !generateEditorCode)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.Classes;
		auto& structInfos = fileInfo.second.Structs;
		auto& enumInfos = fileInfo.second.Enums;

		if (classInfos.empty() && structInfos.empty() && enumInfos.empty())
			continue;

		for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
		{
			if(I->IsFlagSet(ClassFlags::SkipGeneratingCSharp))
				continue;

			body << GenerateCSharpClass(*I);

			if ((I + 1) != classInfos.end() || !structInfos.empty() || !enumInfos.empty())
				body << std::endl;
		}

		for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
		{
			if(I->IsFlagSet(StructFlags::SkipGeneratingCSharp))
				continue;

			body << GenerateCSharpStruct(*I);

			if ((I + 1) != structInfos.end() || !enumInfos.empty())
				body << std::endl;
		}

		for (auto I = enumInfos.begin(); I != enumInfos.end(); ++I)
		{
			body << GenerateCSharpEnum(*I);

			if ((I + 1) != enumInfos.end())
				body << std::endl;
		}

		StringRef csOutputFolder = fileInfo.second.InEditor ? editorOutputFolder : engineOutputFolder;
		std::ofstream output = GeneratorUtility::CreateFile(fileInfo.first + ".generated.cs", csOutputFolder);

		// License/copyright header
		output << GeneratorUtility::GenerateCopyrightHeader(fileInfo.second.InEditor);

		output << "using System;" << std::endl;
		output << "using System.Runtime.CompilerServices;" << std::endl;
		output << "using System.Runtime.InteropServices;" << std::endl;

		if (fileInfo.second.InEditor)
			output << "using " << sFrameworkCsNs << ";" << std::endl;

		output << std::endl;

		if (!fileInfo.second.InEditor)
			output << "namespace " << sFrameworkCsNs << "\n";
		else
			output << "namespace " << sEditorCsNs << "\n";

		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate XML lookup
	XMLMappingGenerator::GenerateMappingXMLFile(false, engineOutputFolder.str());

	if (generateEditorCode)
		XMLMappingGenerator::GenerateMappingXMLFile(true, editorOutputFolder.str());
}
