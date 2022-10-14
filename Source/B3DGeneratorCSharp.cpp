#include "B3DParserUtility.h"
#include "B3DCommentParser.h"
#include "B3DCommon.h"

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
	else if (typeMappingInformation.TypeCategory == ExportedClassTypeCategory::String || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::WString || typeMappingInformation.TypeCategory == ExportedClassTypeCategory::Path)
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

	if (useOutputParameterPrefix && typeInformation.IsOutputParameter())
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
	for (auto& entry : classInfo.ctorInfos)
	{
		if (entry.paramInfos.size() == 0)
			return true;
	}

	// Check external constructors
	for (auto& entry : classInfo.methodInfos)
	{
		bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
		if (!isConstructor)
			continue;

		if (entry.paramInfos.size() == 0)
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
	case VariableTypeCategory::SmallVector:
	case VariableTypeCategory::Array:
		return IsInternalMethodParameterValueType(typeInformation.AssertGetUnderlyingType());
	default: 
		return true;
	}
}

/**
 * Generates C# attributes that control property or field style.
 *
 * @param	style						Information about the style attributes to generate.
 * @param	typeInformation				Native type we're generating the attributes for.
 * @param	typeMappingInformation		Mapping of the provided type in script.
 * @param	isGeneratingStructFields	True if we're generating struct fields, false if generating properties.
 */
static std::string GenerateCSharpStyleAttributes(const ExportStyle& style, const VariableTypeInformation& typeInformation, const TypeMappingInformation& typeMappingInformation, bool isGeneratingStructFields)
{
	std::stringstream output;

	if(((style.flags & (int)StyleFlags::AsLayerMask) != 0) && typeMappingInformation.IsInt64())
		output << "\t\t[LayerMask]\n";

	if ((style.flags & (int)StyleFlags::Step) != 0)
		output << "\t\t[Step(" << style.step << "f)]\n";

	if ((style.flags & (int)StyleFlags::Range) != 0)
	{
		std::string isSlider = ((style.flags & (int)StyleFlags::AsSlider) != 0) ? "true" : "false";
		output << "\t\t[Range(" << style.rangeMin << "f, " << style.rangeMax << "f, " << isSlider << ")]\n";
	}
	else if ((style.flags & (int)StyleFlags::AsSlider) != 0)
		output << "\t\t[Range(float.MinValue, float.MaxValue, true)]\n";

	if(((style.flags & (int)StyleFlags::Order) != 0))
		output << "\t\t[Order(" << style.order << ")]\n";

	if(((style.flags & (int)StyleFlags::Category) != 0))
		output << "\t\t[Category(\"" << style.category << "\")]\n";

	if(((style.flags & (int)StyleFlags::Inline) != 0))
		output << "\t\t[Inline]\n";

	bool notNull = (style.flags & (int)StyleFlags::NotNull) != 0;
	bool passByCopy = (style.flags & (int)StyleFlags::PassByCopy) != 0;

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

	if(((style.flags & (int)StyleFlags::ApplyOnDirty) != 0))
		output << "\t\t[ApplyOnDirty]\n";

	if(((style.flags & (int)StyleFlags::AsQuaternion) != 0))
		output << "\t\t[AsQuaternion]\n";

	if(((style.flags & (int)StyleFlags::LoadOnAssign) != 0))
		output << "\t\t[LoadOnAssign]\n";

	if(((style.flags & (int)StyleFlags::HDR) != 0))
		output << "\t\t[HDR]\n";

	return output.str();
}

/**
 * Generates a default value to assign to a variable, field or parameter.
 *
 * @param variableInformation			Information about the variable, field or parameter to assign the value to.
 * @return								String containing the value to assign, to be placed after the '=' operator.
 */
std::string GenerateCSharpDefaultValueAssignment(const VariableInformation& variableInformation)
{
	if (variableInformation.DefaultValueType.empty() || variableInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags)
		return variableInformation.DefaultValue;
	else
	{
		// Constructor or cast, assuming constructor as cast implies a constructor accepting the type exists (and we don't export cast operators anyway)
		TypeMappingInformation defaultValueTypeMappingInformation = GetNativeToScriptTypeMapping(variableInformation.DefaultValueType);

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
std::string GenerateCSharpMethodParameters(const MethodInfo& methodInfo, bool forInternalMethod)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VariableInformation& paramInfo = *I;

		if(!forInternalMethod && !paramInfo.DefaultValueType.empty() && paramInfo.TypeInformation.TypeCategory != VariableTypeCategory::Flags)
		{
			// We don't generate parameters that have complex default values (as they're not supported in C#).
			// Instead the post-processor has generated different versions of this method, so we can just skip
			// such parameters
			continue;
		}

		if (I != methodInfo.paramInfos.begin())
			output << ", ";

		const TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(paramInfo.TypeInformation);
		const std::string qualifiedType = GetScriptQualifiedType(paramInfo.TypeInformation, parameterTypeMappingInformation, true, forInternalMethod);

		bool isLastParameter = (I + 1) == methodInfo.paramInfos.end();
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
std::string GenerateCSharpMethodArguments(const MethodInfo& methodInfo, bool forInternalMethod)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VariableInformation& parameterInformation = *I;
		const TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);

		if (parameterInformation.TypeInformation.IsOutputParameter())
			output << "out ";
		else if (forInternalMethod && IsStructReference(parameterInformation.TypeInformation, parameterTypeMappingInformation))
			output << "ref ";

		output << parameterInformation.Name;

		if ((I + 1) != methodInfo.paramInfos.end())
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
std::string GenerateCSharpMethodDefaultArgumentAssignments(const MethodInfo& methodInfo, const std::string& indent)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VariableInformation& parameterInformation = *I;

		if (parameterInformation.DefaultValueType.empty() || parameterInformation.TypeInformation.TypeCategory == VariableTypeCategory::Flags)
			continue;

		if (parameterInformation.DefaultValueType == "null" || parameterInformation.DefaultValue == "null")
		{
			TypeMappingInformation paramTypeInfo = GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);
			output << indent << paramTypeInfo.ScriptTypeName << " " << parameterInformation.Name << " = " << parameterInformation.DefaultValue << ";\n";
		}
		else
		{
			TypeMappingInformation defaultValTypeInfo = GetNativeToScriptTypeMapping(parameterInformation.DefaultValueType);
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
std::string GenerateCSharpEventSignature(const MethodInfo& methodInfo)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VariableInformation& paramInfo = *I;
		TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(paramInfo.TypeInformation);
		std::string type = GetScriptQualifiedType(paramInfo.TypeInformation, parameterTypeMappingInformation);

		output << type;

		if ((I + 1) != methodInfo.paramInfos.end())
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
std::string GenerateCSharpEventArguments(const MethodInfo& methodInfo)
{
	std::stringstream output;

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		output << I->Name;

		if ((I + 1) != methodInfo.paramInfos.end())
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
std::string GenerateCSharpInternalMethodSignature(const ClassInfo& classInformation, const MethodInfo& methodInformation, TypeMappingInformation& typeMappingInformation)
{
	const bool isClassModule = (classInformation.flags & (int)ClassFlags::IsModule) != 0;
	const bool isStatic = (methodInformation.flags & (int)MethodFlags::Static) != 0;
	const bool isCtor = (methodInformation.flags & (int)MethodFlags::Constructor) != 0;

	std::stringstream output;

	bool returnAsParameter = false;
	if (methodInformation.returnInfo.TypeInformation.IsEmpty() || isCtor)
		output << "void";
	else
	{
		const TypeMappingInformation returnTypeMappingInformation = GetNativeToScriptTypeMapping(methodInformation.returnInfo.TypeInformation);
		if (!CanBeReturned(methodInformation.returnInfo.TypeInformation, returnTypeMappingInformation))
		{
			output << "void";
			returnAsParameter = true;
		}
		else
		{
			const std::string qualifiedType = GetScriptQualifiedType(methodInformation.returnInfo.TypeInformation, returnTypeMappingInformation);
			output << qualifiedType;
		}
	}

	output << " ";

	output << "Internal_" << methodInformation.interopName << "(";

	if (isCtor)
	{
		output << typeMappingInformation.ScriptTypeName << " managedInstance";

		if (methodInformation.paramInfos.size() > 0)
			output << ", ";
	}
	else if (!isStatic && !isClassModule)
	{
		output << "IntPtr thisPtr";

		if (methodInformation.paramInfos.size() > 0 || returnAsParameter)
			output << ", ";
	}

	output << GenerateCSharpMethodParameters(methodInformation, true);

	if (returnAsParameter)
	{
		const TypeMappingInformation returnTypeMappingInformation = GetNativeToScriptTypeMapping(methodInformation.returnInfo.TypeInformation);
		const std::string qualifiedType = GetScriptQualifiedType(methodInformation.returnInfo.TypeInformation, returnTypeMappingInformation);

		if (methodInformation.paramInfos.size() > 0)
			output << ", ";

		output << "out " << qualifiedType << " __output";
	}

	output << ")";
	return output.str();
}

/**
 * Generates a full declaration of a C# class.
 *
 * @param classInformation			Information about the class to generate.
 * @param typeMappingInformation	Information about the mapping of the native type to script type.
 * @return							Code with the class declaration.
 */
std::string GenerateCSharpClass(const ClassInfo& classInformation, TypeMappingInformation& typeMappingInformation)
{
	const bool isModule = (classInformation.flags & (int)ClassFlags::IsModule) != 0;

	std::stringstream ctors;
	std::stringstream properties;
	std::stringstream events;
	std::stringstream methods;
	std::stringstream interops;

	// Private constructor for runtime use
	MethodInfo privateConstructorInformation = FindUnusedConstructorSignature(classInformation);
	ctors << "\t\tprivate " << typeMappingInformation.ScriptTypeName << "(" << GenerateCSharpMethodParameters(privateConstructorInformation, false) << ") { }" << std::endl;

	// Parameterless constructor in case anything derives from this class
	if (!HasParameterlessConstructor(classInformation))
		ctors << "\t\tprotected " << typeMappingInformation.ScriptTypeName << "() { }" << std::endl;

	ctors << std::endl;

	// Constructors
	for (auto& entry : classInformation.ctorInfos)
	{
		if (!isCSOnly(entry.flags))
		{
			// Generate interop
			interops << GenerateAPICheckBegin(entry.api);
			interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]" << std::endl;
			interops << "\t\tprivate static extern void Internal_" << entry.interopName << "(" << typeMappingInformation.ScriptTypeName << " managedInstance";

			if (entry.paramInfos.size() > 0)
				interops << ", " << GenerateCSharpMethodParameters(entry, true);

			interops << ");\n";
			interops << GenerateApiCheckEnd(entry.api);
		}

		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		if (interopOnly)
			continue;

		ctors << GenerateAPICheckBegin(entry.api);
		ctors << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");

		if (entry.visibility == CSVisibility::Internal)
			ctors << "\t\tinternal ";
		else if (entry.visibility == CSVisibility::Private)
			ctors << "\t\tprivate ";
		else
			ctors << "\t\tpublic ";

		ctors << typeMappingInformation.ScriptTypeName << "(" << GenerateCSharpMethodParameters(entry, false) << ")" << std::endl;
		ctors << "\t\t{" << std::endl;
		ctors << GenerateCSharpMethodDefaultArgumentAssignments(entry, "\t\t\t");
		ctors << "\t\t\tInternal_" << entry.interopName << "(this";

		if (entry.paramInfos.size() > 0)
			ctors << ", " << GenerateCSharpMethodArguments(entry, true);

		ctors << ");" << std::endl;
		ctors << "\t\t}" << std::endl;
		ctors << GenerateApiCheckEnd(entry.api);
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
	for (auto& entry : classInformation.methodInfos)
	{
		// Generate interop
		if (!isCSOnly(entry.flags))
		{
			interops << GenerateAPICheckBegin(entry.api);
			interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]" << std::endl;
			interops << "\t\tprivate static extern " << GenerateCSharpInternalMethodSignature(classInformation, entry, typeMappingInformation) << ";";
			interops << std::endl;
			interops << GenerateApiCheckEnd(entry.api);
		}

		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		if (interopOnly)
			continue;

		bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
		bool isStatic = (entry.flags & (int)MethodFlags::Static) != 0;

		if (isConstructor)
		{
			ctors << GenerateAPICheckBegin(entry.api);
			ctors << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");

			if (entry.visibility == CSVisibility::Internal)
				ctors << "\t\tinternal ";
			else if (entry.visibility == CSVisibility::Private)
				ctors << "\t\tprivate ";
			else
				ctors << "\t\tpublic ";

			ctors << typeMappingInformation.ScriptTypeName << "(" << GenerateCSharpMethodParameters(entry, false) << ")" << std::endl;
			ctors << "\t\t{" << std::endl;
			ctors << GenerateCSharpMethodDefaultArgumentAssignments(entry, "\t\t\t");
			ctors << "\t\t\tInternal_" << entry.interopName << "(this";

			if (entry.paramInfos.size() > 0)
				ctors << ", " << GenerateCSharpMethodArguments(entry, true);

			ctors << ");" << std::endl;
			ctors << "\t\t}" << std::endl;
			ctors << GenerateApiCheckEnd(entry.api);
			ctors << std::endl;
		}
		else
		{
			bool isProperty = entry.flags & ((int)MethodFlags::PropertyGetter | (int)MethodFlags::PropertySetter);
			if (!isProperty)
			{
				TypeMappingInformation returnTypeMappingInformation;
				std::string returnType;
				if (entry.returnInfo.TypeInformation.IsEmpty())
					returnType = "void";
				else
				{
					returnTypeMappingInformation = GetNativeToScriptTypeMapping(entry.returnInfo.TypeInformation);
					returnType = GetScriptQualifiedType(entry.returnInfo.TypeInformation, returnTypeMappingInformation);
				}

				methods << GenerateAPICheckBegin(entry.api);
				methods << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");

				if (entry.visibility == CSVisibility::Internal)
					methods << "\t\tinternal ";
				else if (entry.visibility == CSVisibility::Private)
					methods << "\t\tprivate ";
				else
					methods << "\t\tpublic ";

				if (isStatic || isModule)
					methods << "static ";

				methods << returnType << " " << entry.scriptName << "(" << GenerateCSharpMethodParameters(entry, false) << ")" << std::endl;
				methods << "\t\t{" << std::endl;
				methods << GenerateCSharpMethodDefaultArgumentAssignments(entry, "\t\t\t");

				bool returnByParam = false;
				if (!entry.returnInfo.TypeInformation.IsEmpty())
				{
					if (!CanBeReturned(entry.returnInfo.TypeInformation, returnTypeMappingInformation))
					{
						methods << "\t\t\t" << returnType << " temp;" << std::endl;
						methods << "\t\t\tInternal_" << entry.interopName << "(";
						returnByParam = true;
					}
					else
						methods << "\t\t\treturn Internal_" << entry.interopName << "(";
				}
				else
					methods << "\t\t\tInternal_" << entry.interopName << "(";

				if (!isStatic && !isModule)
				{
					methods << "mCachedPtr";

					if (entry.paramInfos.size() > 0 || returnByParam)
						methods << ", ";
				}

				methods << GenerateCSharpMethodArguments(entry, true);

				if (returnByParam)
				{
					if (entry.paramInfos.size() > 0)
						methods << ", ";

					methods << "out temp";
				}

				methods << ");" << std::endl;

				if (returnByParam)
					methods << "\t\t\treturn temp;" << std::endl;

				methods << "\t\t}" << std::endl;
				methods << GenerateApiCheckEnd(entry.api);
				methods << std::endl;
			}
		}
	}

	// Properties
	for (auto& entry : classInformation.propertyInfos)
	{
		const TypeMappingInformation propertyTypeMappingInformation = GetNativeToScriptTypeMapping(entry.TypeInformation);
		const std::string propertyQualifiedTypeName = GetScriptQualifiedType(entry.TypeInformation, propertyTypeMappingInformation);

		properties << GenerateAPICheckBegin(entry.api);
		properties << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");

		bool defaultVisible = entry.visibility != CSVisibility::Internal && entry.visibility != CSVisibility::Private &&
			!entry.setter.empty();
		if (defaultVisible)
		{
			if ((entry.style.flags & (int)StyleFlags::ForceHide) == 0)
				properties << "\t\t[ShowInInspector]" << std::endl;
		}
		else
		{
			if ((entry.style.flags & (int)StyleFlags::ForceShow) != 0)
				properties << "\t\t[ShowInInspector]" << std::endl;
		}

		properties << GenerateCSharpStyleAttributes(entry.style, entry.TypeInformation, propertyTypeMappingInformation, false);

		properties << "\t\t[NativeWrapper]\n";

		if (entry.visibility == CSVisibility::Internal)
			properties << "\t\tinternal ";
		else if (entry.visibility == CSVisibility::Private)
			properties << "\t\tprivate ";
		else
			properties << "\t\tpublic ";

		if (entry.isStatic || isModule)
			properties << "static ";

		properties << propertyQualifiedTypeName << " " << entry.name << std::endl;
		properties << "\t\t{" << std::endl;

		if (!entry.getter.empty())
		{
			if (CanBeReturned(entry.TypeInformation, propertyTypeMappingInformation))
			{
				properties << "\t\t\tget { return Internal_" << entry.getter << "(";

				if (!entry.isStatic && !isModule)
					properties << "mCachedPtr";

				properties << "); }" << std::endl;
			}
			else
			{
				properties << "\t\t\tget" << std::endl;
				properties << "\t\t\t{" << std::endl;
				properties << "\t\t\t\t" << propertyQualifiedTypeName << " temp;" << std::endl;

				properties << "\t\t\t\tInternal_" << entry.getter << "(";

				if (!entry.isStatic && !isModule)
					properties << "mCachedPtr, ";

				properties << "out temp);" << std::endl;

				properties << "\t\t\t\treturn temp;" << std::endl;
				properties << "\t\t\t}" << std::endl;
			}
		}

		if (!entry.setter.empty())
		{
			properties << "\t\t\tset { Internal_" << entry.setter << "(";

			if (!entry.isStatic && !isModule)
				properties << "mCachedPtr, ";

			if(IsStructReference(entry.TypeInformation, propertyTypeMappingInformation))
				properties << "ref ";

			properties << "value); }" << std::endl;
		}

		properties << "\t\t}" << std::endl;
		properties << GenerateApiCheckEnd(entry.api);
		properties << std::endl;
	}

	// Events & callbacks
	for(auto& entry : classInformation.eventInfos)
	{
		bool isStatic = (entry.flags & (int)MethodFlags::Static) != 0;
		bool isCallback = (entry.flags & (int)MethodFlags::Callback) != 0;
		bool isInternal = (entry.flags & (int)MethodFlags::InteropOnly) != 0;

		events << GenerateAPICheckBegin(entry.api);
		events << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");
		events << "\t\t";

		if (!isCallback && !isInternal)
		{
			if (entry.visibility == CSVisibility::Internal)
				events << "internal ";
			else if (entry.visibility == CSVisibility::Private)
				events << "private ";
			else
				events << "public ";
		}

		if (isStatic || isModule)
			events << "static ";

		if (!isCallback && !isInternal)
		{
			events << "event Action";

			if (!entry.paramInfos.empty())
				events << "<" << GenerateCSharpEventSignature(entry) << ">";

			events << " " << entry.scriptName << ";\n\n";
		}
		else
		{
			events << "partial void Callback_" << entry.scriptName << "(";

			if (!entry.paramInfos.empty())
				events << GenerateCSharpMethodParameters(entry, false);

			events << ");\n";
			events << GenerateApiCheckEnd(entry.api);
			events << "\n";
		}

		// Event interop
		interops << GenerateAPICheckBegin(entry.api);

		interops << "\t\tprivate ";

		if (isStatic || isModule)
			interops << "static ";

		interops << "void Internal_" << entry.interopName << "(" << GenerateCSharpMethodParameters(entry, true) << ")" << std::endl;
		interops << "\t\t{" << std::endl;
		if (!isCallback && !isInternal)
			interops << "\t\t\t" << entry.scriptName << "?.Invoke(" << GenerateCSharpEventArguments(entry) << ");\n";
		else
			interops << "\t\t\tCallback_" << entry.scriptName << "(" << GenerateCSharpEventArguments(entry) << ");\n";
		interops << "\t\t}" << std::endl;
		interops << GenerateApiCheckEnd(entry.api);
	}

	std::stringstream output;
	output << GenerateAPICheckBegin(classInformation.api);

	if(!classInformation.module.empty())
	{
		output << "\t/** @addtogroup " << classInformation.module << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << CommentParser::GenerateXMLComments(classInformation.documentation, "\t");

	// Force non-resource and non-component types to show in inspector, except explicitly hidden
	if (typeMappingInformation.IsClassType() || (classInformation.flags & (int)ClassFlags::HideInInspector) == 0)
		output << "\t[ShowInInspector]\n";

	if (classInformation.visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (classInformation.visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (classInformation.visibility == CSVisibility::Private)
		output << "\tprivate ";
	else
		output << "\t";

	std::string baseType;
	if (!classInformation.baseClass.empty())
	{
		TypeMappingInformation baseTypeInfo = GetNativeToScriptTypeMapping(classInformation.baseClass);
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

	if(!classInformation.module.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << GenerateApiCheckEnd(classInformation.api);

	return output.str();
}

std::string generateCSStruct(StructInfo& input)
{
	std::stringstream output;
	output << GenerateAPICheckBegin(input.api);

	if(!input.module.empty())
	{
		output << "\t/** @addtogroup " << input.module << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << CommentParser::GenerateXMLComments(input.documentation, "\t");

	output << "\t[StructLayout(LayoutKind.Sequential), SerializeObject]\n";

	if (input.visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (input.visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (input.visibility == CSVisibility::Private)
		output << "\tprivate ";
	else
		output << "\t";

	std::string scriptName = NativeToScriptTypeMap[input.name].ScriptTypeName;
	output << "partial struct " << scriptName;

	output << std::endl;
	output << "\t{" << std::endl;

	for (auto& entry : input.ctors)
	{
		bool isParameterless = entry.params.size() == 0;
		if (isParameterless) // Parameterless constructors not supported on C# structs
		{
			output << "\t\t/// <summary>Initializes the struct with default values.</summary>" << std::endl;
			output << "\t\tpublic static " << scriptName << " Default(";
		}
		else
		{
			output << CommentParser::GenerateXMLComments(entry.documentation, "\t\t");
			output << "\t\tpublic " << scriptName << "(";
		}

		for (auto I = entry.params.begin(); I != entry.params.end(); ++I)
		{
			const VariableInformation& parameterInformation = *I;
			const TypeMappingInformation parameterTypeMappingInformation = GetNativeToScriptTypeMapping(parameterInformation.TypeInformation);

			if (parameterInformation.TypeInformation.IsOutputParameter())
			{
				// We report the error during field generation, as it checks for the same condition
				continue;
			}


			if(!parameterInformation.DefaultValueType.empty() && parameterInformation.TypeInformation.TypeCategory != VariableTypeCategory::Flags)
			{
				// We don't generate parameters that have complex default values (as they're not supported in C#).
				// Instead the post-processor has generated different versions of this method, so we can just skip
				// such parameters
				continue;
			}

			output << parameterTypeMappingInformation.ScriptTypeName << " " << parameterInformation.Name;

			if (!parameterInformation.DefaultValue.empty())
				output << " = " << GenerateCSharpDefaultValueAssignment(parameterInformation);

			if ((I + 1) != entry.params.end())
				output << ", ";
		}

		output << ")" << std::endl;
		output << "\t\t{" << std::endl;

		std::string thisPtr;
		if (isParameterless)
		{
			output << "\t\t\t" << scriptName << " value = new " << scriptName << "();" << std::endl;
			thisPtr = "value";
		}
		else
			thisPtr = "this";

		for (auto I = input.fields.begin(); I != input.fields.end(); ++I)
		{
			const VariableInformation& fieldInformation = *I;
			const TypeMappingInformation fieldTypeMappingInformation = GetNativeToScriptTypeMapping(fieldInformation.TypeInformation);
			if (fieldInformation.TypeInformation.IsOutputParameter())
			{
				// We report the error during field generation, as it checks for the same condition
				continue;
			}

			std::string fieldName = fieldInformation.Name;

			auto iterFind = entry.fieldAssignments.find(fieldInformation.Name);
			if (iterFind != entry.fieldAssignments.end())
			{
				std::string paramName = iterFind->second;
				output << "\t\t\t" << thisPtr << "." << fieldName << " = " << paramName << ";" << std::endl;
			}
			else
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

	if(!input.baseClass.empty())
	{
		TypeMappingInformation baseTypeInfo = GetNativeToScriptTypeMapping(input.baseClass);
		StructInfo* baseStructInfo = FindStructInformation(input.baseClass);
		if (baseStructInfo != nullptr)
		{
			// GetBase()
			output << "\t\t///<summary>\n";
			output << "\t\t/// Returns a subset of this struct. This subset usually contains common fields shared with another struct.\n";
			output << "\t\t///</summary>\n";
			output << "\t\tpublic " << baseTypeInfo.ScriptTypeName << " GetBase()\n";
			output << "\t\t{\n";
			output << "\t\t\t" << baseTypeInfo.ScriptTypeName << " value;\n";

			for (auto I = baseStructInfo->fields.begin(); I != baseStructInfo->fields.end(); ++I)
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

			for (auto I = baseStructInfo->fields.begin(); I != baseStructInfo->fields.end(); ++I)
			{
				const FieldInfo& fieldInfo = *I;
				output << "\t\t\t" << fieldInfo.Name << " = value." << fieldInfo.Name << ";\n";
			}

			output << "\t\t}\n";
			output << "\n";
		}
	}

	for (auto I = input.fields.begin(); I != input.fields.end(); ++I)
	{
		const FieldInfo& fieldInformation = *I;
		const TypeMappingInformation fieldTypeMappingInformation = GetNativeToScriptTypeMapping(fieldInformation.TypeInformation);

		if (fieldInformation.TypeInformation.IsOutputParameter())
		{
			outs() << "Error: Invalid field type found in struct \"" << scriptName << "\" for field \"" << fieldInformation.Name << "\". Skipping.\n";
			continue;
		}

		output << CommentParser::GenerateXMLComments(fieldInformation.documentation, "\t\t");
		output << GenerateCSharpStyleAttributes(fieldInformation.style, fieldInformation.TypeInformation, fieldTypeMappingInformation, true);

		if ((fieldInformation.style.flags & (int)StyleFlags::ForceHide) != 0)
			output << "\t\t[HideInInspector]" << std::endl;

		output << "\t\tpublic ";

		output << fieldTypeMappingInformation.ScriptTypeName;
		if (fieldInformation.TypeInformation.IsArrayOrVector())
			output << "[]";

		output << " ";
		output << fieldInformation.Name;

		output << ";" << std::endl;
	}

	output << "\t}" << std::endl;

	if(!input.module.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << GenerateApiCheckEnd(input.api);
	return output.str();
}

std::string generateCSEnum(EnumInfo& input)
{
	std::stringstream output;
	output << GenerateAPICheckBegin(input.api);

	if(!input.module.empty())
	{
		output << "\t/** @addtogroup " << input.module << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << CommentParser::GenerateXMLComments(input.documentation, "\t");
	if (input.visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (input.visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (input.visibility == CSVisibility::Private)
		output << "\tprivate ";

	output << "enum " << input.scriptName;

	if (!input.explicitType.empty())
		output << " : " << input.explicitType;

	output << std::endl;
	output << "\t{" << std::endl;

	for (auto I = input.entries.begin(); I != input.entries.end(); ++I)
	{
		if (I != input.entries.begin())
			output << ",\n";

		const EnumEntryInfo& entryInfo = I->second;

		output << CommentParser::GenerateXMLComments(entryInfo.Documentation, "\t\t");
		output << "\t\t" << entryInfo.ScriptName;
		output << " = ";
		output << entryInfo.Value;
	}

	output << "\n";
	output << "\t}" << std::endl;

	if(!input.module.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << GenerateApiCheckEnd(input.api);
	return output.str();
}

std::string generateXMLParamInfo(const VariableInformation& varInfo, const CommentEntry& methodDoc, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<param name=\"" << escapeXML(varInfo.Name) << "\" type=\"" <<
		escapeXML(GetNativeToScriptTypeMapping(varInfo.TypeInformation).ScriptTypeName) << "\">\n";

	auto iterFind = std::find_if(methodDoc.params.begin(), methodDoc.params.end(),
		[&varName = varInfo.Name](const CommentParameterEntry& entry) { return varName == entry.Name; });
	if (iterFind != methodDoc.params.end() && !iterFind->comments.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(iterFind->comments) << "</doc>\n";

	output << indent << "</param>\n";
	return output.str();
}

std::string generateXMLFieldInfo(const FieldInfo& fieldInfo, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<field name=\"" << escapeXML(fieldInfo.Name) << "\" type=\"" <<
		escapeXML(GetNativeToScriptTypeMapping(fieldInfo.TypeInformation).ScriptTypeName) << "\">\n";

	// TODO - Generate inspector visibility
	if(!fieldInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(fieldInfo.documentation.brief) << "</doc>\n";

	output << indent << "</field>\n";
	return output.str();
}

std::string generateXMLMethodInfo(const MethodInfo& methodInfo, const std::string& indent, bool ctor)
{
	std::stringstream output;

   std::string isStaticStr = "false";
   bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
   if(!ctor && isStatic)
	   isStaticStr = "true";

	if(!ctor)
	{
		output << indent << "<method native=\"" << escapeXML(methodInfo.sourceName) << "\" script=\"" <<
			escapeXML(methodInfo.scriptName) << "\" static=\"" << isStaticStr << "\">\n";
	}
	else
		output << indent << "<ctor>\n";

	if(!methodInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(methodInfo.documentation.brief) << "</doc>\n";

	for(auto& param : methodInfo.paramInfos)
		output << generateXMLParamInfo(param, methodInfo.documentation, indent + "\t");

	if(!ctor && !methodInfo.returnInfo.TypeInformation.IsEmpty())
	{
		output << indent << "\t<returns type=\"" << escapeXML(GetNativeToScriptTypeMapping(methodInfo.returnInfo.TypeInformation).ScriptTypeName) << "\">\n";

		if (!methodInfo.documentation.returns.empty())
			output << indent << "\t\t<doc>" << CommentParser::GenerateXMLCommentText(methodInfo.documentation.returns) << "</doc>\n";

		output << indent << "\t</returns>\n";
	}

	if(!ctor)
		output << indent << "</method>\n";
	else
		output << indent << "</ctor>\n";

	return output.str();
}

std::string generateXMLMethodInfo(const SimpleConstructorInfo& methodInfo, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<ctor>\n";
	if(!methodInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(methodInfo.documentation.brief) << "</doc>\n";

	for(auto& param : methodInfo.params)
		output << generateXMLParamInfo(param, methodInfo.documentation, indent + "\t");

	output << indent << "</ctor>\n";
	return output.str();
}

std::string generateXMLPropertyInfo(const PropertyInfo& propertyInfo, const std::string& indent)
{
	std::string staticStr = propertyInfo.isStatic ? "true" : "false";

	std::stringstream output;
	output << indent << "<property name=\"" << escapeXML(propertyInfo.name) << "\" type=\"" <<
		escapeXML(GetNativeToScriptTypeMapping(propertyInfo.TypeInformation).ScriptTypeName) <<
		"\" getter=\"" << escapeXML(propertyInfo.getter) << "\" setter=\"" << escapeXML(propertyInfo.setter) <<
		"\" static=\"" << staticStr << "\">\n";

	// TODO - Generate inspector visibility
	if(!propertyInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(propertyInfo.documentation.brief) << "</doc>\n";

	output << indent << "</property>\n";
	return output.str();
}

std::string generateXMLEventInfo(const MethodInfo& eventInfo, const std::string& indent)
{
   bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
   std::string staticStr = isStatic ? "true" : "false";

	std::stringstream output;
	output << indent << "<event native=\"" << escapeXML(eventInfo.sourceName) << "\" script=\"" << escapeXML(eventInfo.scriptName) <<
		"\" static=\"" << staticStr << "\">\n";

	// TODO - Generate inspector visibility
	if (!eventInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(eventInfo.documentation.brief) << "</doc>\n";

	for(auto& param : eventInfo.paramInfos)
		output << generateXMLParamInfo(param, eventInfo.documentation, indent + "\t");

	if(!eventInfo.returnInfo.TypeInformation.IsEmpty())
	{
		output << indent << "\t<returns type=\"" << escapeXML(GetNativeToScriptTypeMapping(eventInfo.returnInfo.TypeInformation).ScriptTypeName) << "\">\n";

		if (!eventInfo.documentation.returns.empty())
			output << indent << "\t\t<doc>" << CommentParser::GenerateXMLCommentText(eventInfo.documentation.returns) << "</doc>\n";

		output << indent << "\t</returns>\n";
	}

	output << indent << "</event>\n";
	return output.str();
}

std::string generateXMLEnum(EnumInfo& input, const std::string& indent)
{
	std::stringstream output;

	output << indent << "<enum native=\"" << escapeXML(input.name) << "\" script=\"" << escapeXML(input.scriptName) << "\">\n";
	if (!input.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(input.documentation.brief) << "</doc>\n";

	for (auto I = input.entries.begin(); I != input.entries.end(); ++I)
	{
		const EnumEntryInfo& entryInfo = I->second;

	   output << indent << "\t<enumentry native=\"" << escapeXML(entryInfo.NativeName) << "\" script=\"" << escapeXML(entryInfo.ScriptName) << "\">\n";
	   if (!entryInfo.Documentation.brief.empty())
		   output << indent << "\t\t<doc>" << CommentParser::GenerateXMLCommentText(entryInfo.Documentation.brief) << "</doc>\n";
	   output << indent << "\t</enumentry>\n";
	}

	output << indent << "</enum>\n";
	return output.str();
}

std::string generateXMLStruct(StructInfo& input, const std::string& indent)
{
	std::stringstream output;

	TypeMappingInformation& typeInfo = NativeToScriptTypeMap[input.name];

	output << indent << "<struct native=\"" << escapeXML(input.name) << "\" script=\"" << escapeXML(typeInfo.ScriptTypeName) << "\">\n";
	if (!input.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(input.documentation.brief) << "</doc>\n";

	for (auto& entry : input.ctors)
		output << generateXMLMethodInfo(entry, indent + "\t");

	for(auto& entry : input.fields)
	  output << generateXMLFieldInfo(entry, indent + "\t");

	output << indent << "</struct>\n";
	return output.str();
}

std::string generateXMLClass(ClassInfo& input, bool editor, const std::string& indent)
{
	std::stringstream output;

	TypeMappingInformation& typeInfo = NativeToScriptTypeMap[input.name];

	output << indent << "<class native=\"" << escapeXML(input.name) << "\" script=\"" << escapeXML(typeInfo.ScriptTypeName) << "\">\n";
	if (!input.documentation.brief.empty())
		output << indent << "\t<doc>" << CommentParser::GenerateXMLCommentText(input.documentation.brief) << "</doc>\n";

	for (auto& entry : input.ctorInfos)
	{
		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		if(IsAPIValid(entry.api, editor) && !interopOnly)
			output << generateXMLMethodInfo(entry, indent + "\t", true);
	}

	for(auto& entry : input.methodInfos)
	{
		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
		bool isProperty = entry.flags & ((int)MethodFlags::PropertyGetter | (int)MethodFlags::PropertySetter);

		if(IsAPIValid(entry.api, editor) && !interopOnly && !isProperty)
			output << generateXMLMethodInfo(entry, indent + "\t", isConstructor);
	}

   for(auto& entry : input.propertyInfos)
   {
		if(IsAPIValid(entry.api, editor))
			output << generateXMLPropertyInfo(entry, indent + "\t");
   }

   for(auto& entry : input.eventInfos)
   {
	   bool isCallback = (entry.flags & (int)MethodFlags::Callback) != 0;
	   bool isInternal = (entry.flags & (int)MethodFlags::InteropOnly) != 0;

	  if(!isCallback && !isInternal)
		  output << generateXMLEventInfo(entry, indent + "\t");
   }

	output << indent << "</class>\n";
	return output.str();
}

void generateMappingXMLFile(bool editor, const std::string& outputFolder)
{
	std::stringstream body;
	for (auto& fileInfo : outputFileInfos)
	{
		auto& enumInfos = fileInfo.second.enumInfos;
		for (auto& entry : enumInfos)
		{
			if (IsAPIValid(entry.api, editor))
				body << generateXMLEnum(entry, "\t");
		}

		auto& structInfos = fileInfo.second.structInfos;
		for (auto& entry : structInfos)
		{
			if (IsAPIValid(entry.api, editor))
				body << generateXMLStruct(entry, "\t");
		}


		auto& classInfos = fileInfo.second.classInfos;
		for (auto& entry : classInfos)
		{
			if (IsAPIValid(entry.api, editor))
				body << generateXMLClass(entry, editor, "\t");
		}
	}

	std::ofstream output = createFile("info.xml", outputFolder);

	output << "<?xml version='1.0' encoding='UTF-8' standalone='no'?>\n";
	output << "<entries>\n";
	output << body.str();
	output << "</entries>\n";
	output.close();
}

void GenerateCSharp(StringRef engineOutputFolder, StringRef editorOutputFolder, bool generateEditorCode)
{
	cleanAndPrepareFolder(engineOutputFolder);

	if (generateEditorCode)
	{
		cleanAndPrepareFolder(editorOutputFolder);
	}

	// Generate CS
	for (auto& fileInfo : outputFileInfos)
	{
		if (fileInfo.second.inEditor && !generateEditorCode)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.classInfos;
		auto& structInfos = fileInfo.second.structInfos;
		auto& enumInfos = fileInfo.second.enumInfos;

		if (classInfos.empty() && structInfos.empty() && enumInfos.empty())
			continue;

		for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
		{
			ClassInfo& classInfo = *I;
			TypeMappingInformation& typeInfo = NativeToScriptTypeMap[classInfo.name];

			body << GenerateCSharpClass(classInfo, typeInfo);

			if ((I + 1) != classInfos.end() || !structInfos.empty() || !enumInfos.empty())
				body << std::endl;
		}

		for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
		{
			body << generateCSStruct(*I);

			if ((I + 1) != structInfos.end() || !enumInfos.empty())
				body << std::endl;
		}

		for (auto I = enumInfos.begin(); I != enumInfos.end(); ++I)
		{
			body << generateCSEnum(*I);

			if ((I + 1) != enumInfos.end())
				body << std::endl;
		}

		StringRef csOutputFolder = fileInfo.second.inEditor ? editorOutputFolder : engineOutputFolder;
		std::ofstream output = createFile(fileInfo.first + ".generated.cs", csOutputFolder);

		// License/copyright header
		output << generateFileHeader(fileInfo.second.inEditor);

		output << "using System;" << std::endl;
		output << "using System.Runtime.CompilerServices;" << std::endl;
		output << "using System.Runtime.InteropServices;" << std::endl;

		if (fileInfo.second.inEditor)
			output << "using " << sFrameworkCsNs << ";" << std::endl;

		output << std::endl;

		if (!fileInfo.second.inEditor)
			output << "namespace " << sFrameworkCsNs << "\n";
		else
			output << "namespace " << sEditorCsNs << "\n";

		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate XML lookup
	generateMappingXMLFile(false, engineOutputFolder.str());

	if (generateEditorCode)
		generateMappingXMLFile(true, editorOutputFolder.str());
}
