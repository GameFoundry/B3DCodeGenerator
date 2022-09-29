#include "B3DCommon.h"
#include "B3DParser.h"
#include "B3DParserUtility.h"

#define B3DCODEGEN_WAIT_FOR_DEBUGGER 0
#if B3DCODEGEN_WAIT_FOR_DEBUGGER
#include <windows.h>
#endif

const char *const kBuiltinComponentType = "Component";
const char *const kBuiltinSceneObjectType = "SceneObject";
const char *const kBuiltinResourceType = "Resource";
const char *const kBuiltinModuleType = "Module";
const char *const kBuiltinGUIElementType = "GUIElement";
const char *const kBuiltinReflectableType = "IReflectable";

std::string sFrameworkCppNs = "bs";
std::string sEditorCppNs = "bs";
std::string sFrameworkCsNs = "bs";
std::string sEditorCsNs = "bs.Editor";
std::string sFrameworkExportMacro = "BS_SCR_BE_EXPORT";
std::string sEditorExportMacro = "BS_SCR_BED_EXPORT";
std::string sFrameworkCopyrightNotice = 
	"//********************************* bs::framework - Copyright 2018-2019 Marko Pintera ************************************//\n" \
	"//*********** Licensed under the MIT license. See LICENSE.md for full terms. This notice is not to be removed. ***********//\n";
std::string sEditorCopyrightNotice = 
	"//********************************** Banshee Engine (www.banshee3d.com) **************************************************//\n" \
	"//************** Copyright (c) 2016-2019 Marko Pintera (marko.pintera@gmail.com). All rights reserved. *******************//\n";

std::unordered_map<std::string, TypeMappingInformation> NativeToScriptTypeMap;
std::unordered_map<std::string, FileInfo> outputFileInfos;
std::unordered_map<std::string, ExternalClassInfos> externalClassInfos;
std::unordered_map<std::string, BaseClassInfo> baseClassLookup;

std::vector<CommentInformation> commentInfos;
std::unordered_map<std::string, int> commentFullLookup;
std::unordered_map<std::string, SmallVector<int, 2>> commentSimpleLookup;

static cl::OptionCategory OptCategory("Script binding options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp CustomHelp("\nAdd \"-- <compiler arguments>\" at the end to setup the compiler "
	"invocation\n");

static cl::opt<std::string> OutputCppEngineOption(
	"output-cpp",
	cl::desc("Specify output directory. Generated non-editor CPP files will be placed into that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> OutputCppEditorOption(
	"output-cpp-editor",
	cl::desc("Specify output directory. Generated editor CPP files will be placed into that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> OutputCSEngineOption(
	"output-cs",
	cl::desc("Specify output directory. Generated non-editor CS files will be placed relative to that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> OutputCSEditorOption(
	"output-cs-editor",
	cl::desc("Specify output directory. Generated editor CS files will be placed relative to that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<bool> GenerateEditorOption(
	"gen-editor",
	cl::desc("If enabled the script code marked with BED API will be generated as well.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppFrameworkNamespaceOption(
	"cpp-framework-ns",
	cl::desc("Specify namespace to place generated C++ framework types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppEditorNamespaceOption(
	"cpp-editor-ns",
	cl::desc("Specify namespace to place generated C++ editor types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CsFrameworkNamespaceOption(
	"cs-framework-ns",
	cl::desc("Specify namespace to place generated C# framework types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CsEditorNamespaceOption(
	"cs-editor-ns",
	cl::desc("Specify namespace to place generated C# editor types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppFrameworkExportMacroOption(
	"cpp-framework-export-macro",
	cl::desc("Specify DLL export macro to use for generated C++ framework types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppEditorExportMacroOption(
	"cpp-editor-export-macro",
	cl::desc("Specify DLL export macro to use for generated C++ editor types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppFrameworkCopyrightNoticeOption(
	"framework-copyright-notice",
	cl::desc("Specify copyright notice to add to the header of all generated framework files.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppEditorCopyrightNoticeOption(
	"editor-copyright-notice",
	cl::desc("Specify copyright notice to add to the header of all generated editor files.\n"),
	cl::cat(OptCategory));

class BansheeCodeGeneratorASTConsumer : public ASTConsumer 
{
public:
	explicit BansheeCodeGeneratorASTConsumer(CompilerInstance* CI, CommentParser& commentParser)
		: visitor(new BansheeCodeGeneratorASTVisitor(CI, commentParser))
	{
		
	}

	~BansheeCodeGeneratorASTConsumer()
	{
		delete visitor;
	}

	void HandleTranslationUnit(ASTContext& Context) override
	{
		visitor->TraverseDecl(Context.getTranslationUnitDecl());
	}

private:
	BansheeCodeGeneratorASTVisitor *visitor;
};

class BansheeCodeGeneratorFrontendAction : public ASTFrontendAction 
{
public:
	BansheeCodeGeneratorFrontendAction(CommentParser& commentParser)
		:mCommentParser(commentParser)
	{ }

	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI, StringRef file) override
	{
		mCommentParser.SetASTContext(CI.getASTContext());
		return std::make_unique<BansheeCodeGeneratorASTConsumer>(&CI, mCommentParser);
	}

private:
	CommentParser mCommentParser;
};

class BansheeCodeGeneratorFrontendActionFactory : public FrontendActionFactory
{
public:
	/** Returns a helper class that can be used for parsing comments. */
	CommentParser& GetCommentParser() { return mCommentParser; }

	std::unique_ptr<FrontendAction> create() override
	{
		return std::make_unique<BansheeCodeGeneratorFrontendAction>(mCommentParser);
	}

private:
	CommentParser mCommentParser;
};

int main(int argc, const char** argv)
{
#if B3DCODEGEN_WAIT_FOR_DEBUGGER
	// Waits before starting the code-gen, to allow the debugger to attach (Windows only)
	// Requires <windows.h> header
	 while (!::IsDebuggerPresent())
	{
		::Sleep(100);
	}
	::DebugBreak();
#endif

	Expected<CommonOptionsParser> op = CommonOptionsParser::create(argc, argv, OptCategory);
	if(auto error = op.takeError())
	{
		errs() << "Error creating command line options parser: " << toString(std::move(error));
		return 1;
	}

	CommonOptionsParser& optionsParser = *op;
	ClangTool Tool(optionsParser.getCompilations(), optionsParser.getSourcePathList());

	if (!CppFrameworkNamespaceOption.getValue().empty())
		sFrameworkCppNs = std::string(CppFrameworkNamespaceOption.getValue().c_str());
	
	if (!CppEditorNamespaceOption.getValue().empty())
		sEditorCppNs = std::string(CppEditorNamespaceOption.getValue().c_str());
	
	if (!CsFrameworkNamespaceOption.getValue().empty())
		sFrameworkCsNs = std::string(CsFrameworkNamespaceOption.getValue().c_str());
	
	if (!CsEditorNamespaceOption.getValue().empty())
		sEditorCsNs = std::string(CsEditorNamespaceOption.getValue().c_str());
	
	if (!CppFrameworkExportMacroOption.getValue().empty())
		sFrameworkExportMacro = std::string(CppFrameworkExportMacroOption.getValue().c_str());
	
	if (!CppEditorExportMacroOption.getValue().empty())
		sEditorExportMacro = std::string(CppEditorExportMacroOption.getValue().c_str());

	if (!CppFrameworkCopyrightNoticeOption.empty())
		sFrameworkCopyrightNotice = std::string(CppFrameworkCopyrightNoticeOption.getValue().c_str());
	
	if (!CppEditorCopyrightNoticeOption.empty())
		sEditorCopyrightNotice = std::string(CppEditorCopyrightNoticeOption.getValue().c_str());
	
	// Note: I could auto-generate C++ wrappers for these types
	SmallVector<std::string, 4> frameworkNs = { sFrameworkCppNs };
	
	NativeToScriptTypeMap["Vector2"] = TypeMappingInformation(frameworkNs,"Vector2", ::ExportedClassTypeCategory::Struct, "Math/BsVector2.h", "Wrappers/BsScriptVector.h");
	NativeToScriptTypeMap["Vector3"] = TypeMappingInformation(frameworkNs, "Vector3", ::ExportedClassTypeCategory::Struct, "Math/BsVector3.h", "Wrappers/BsScriptVector.h");
	NativeToScriptTypeMap["Vector4"] = TypeMappingInformation(frameworkNs, "Vector4", ::ExportedClassTypeCategory::Struct, "Math/BsVector4.h", "Wrappers/BsScriptVector.h");
	NativeToScriptTypeMap["Matrix3"] = TypeMappingInformation(frameworkNs, "Matrix3", ::ExportedClassTypeCategory::Struct, "Math/BsMatrix3.h", "");
	NativeToScriptTypeMap["Matrix4"] = TypeMappingInformation(frameworkNs, "Matrix4", ::ExportedClassTypeCategory::Struct, "Math/BsMatrix4.h", "");
	NativeToScriptTypeMap["Quaternion"] = TypeMappingInformation(frameworkNs, "Quaternion", ::ExportedClassTypeCategory::Struct, "Math/BsQuaternion.h", "Wrappers/BsScriptQuaternion.h");
	NativeToScriptTypeMap["Radian"] = TypeMappingInformation(frameworkNs, "Radian", ::ExportedClassTypeCategory::Struct, "Math/BsRadian.h", "");
	NativeToScriptTypeMap["Degree"] = TypeMappingInformation(frameworkNs, "Degree", ::ExportedClassTypeCategory::Struct, "Math/BsDegree.h", "");
	NativeToScriptTypeMap["Color"] = TypeMappingInformation(frameworkNs, "Color", ::ExportedClassTypeCategory::Struct, "Image/BsColor.h", "Wrappers/BsScriptColor.h");
	NativeToScriptTypeMap["AABox"] = TypeMappingInformation(frameworkNs, "AABox", ::ExportedClassTypeCategory::Struct, "Math/BsAABox.h", "");
	NativeToScriptTypeMap["Sphere"] = TypeMappingInformation(frameworkNs, "Sphere", ::ExportedClassTypeCategory::Struct, "Math/BsSphere.h", "");
	NativeToScriptTypeMap["Capsule"] = TypeMappingInformation(frameworkNs, "Capsule", ::ExportedClassTypeCategory::Struct, "Math/BsCapsule.h", "");
	NativeToScriptTypeMap["Ray"] = TypeMappingInformation(frameworkNs, "Ray", ::ExportedClassTypeCategory::Struct, "Math/BsRay.h", "");
	NativeToScriptTypeMap["Vector2I"] = TypeMappingInformation(frameworkNs, "Vector2I", ::ExportedClassTypeCategory::Struct, "Math/BsVector2I.h", "Wrappers/BsScriptVector2I.h");
	NativeToScriptTypeMap["Rect2"] = TypeMappingInformation(frameworkNs, "Rect2", ::ExportedClassTypeCategory::Struct, "Math/BsRect2.h", "");
	NativeToScriptTypeMap["Rect2I"] = TypeMappingInformation(frameworkNs, "Rect2I", ::ExportedClassTypeCategory::Struct, "Math/BsRect2I.h", "");
	NativeToScriptTypeMap["Bounds"] = TypeMappingInformation(frameworkNs, "Bounds", ::ExportedClassTypeCategory::Struct, "Math/BsBounds.h", "");
	NativeToScriptTypeMap["Plane"] = TypeMappingInformation(frameworkNs, "Plane", ::ExportedClassTypeCategory::Struct, "Math/BsPlane.h", "Wrappers/BsScriptPlane.h");
	NativeToScriptTypeMap["UUID"] = TypeMappingInformation(frameworkNs, "UUID", ::ExportedClassTypeCategory::Struct, "Utility/BsUUID.h", "");
	NativeToScriptTypeMap["SceneObject"] = TypeMappingInformation(frameworkNs, "SceneObject", ::ExportedClassTypeCategory::SceneObject, "Scene/BsSceneObject.h", "Wrappers/BsScriptSceneObject.h");
	NativeToScriptTypeMap["Resource"] = TypeMappingInformation(frameworkNs, "Resource", ::ExportedClassTypeCategory::Resource, "Resources/BsResource.h", "Wrappers/BsScriptResource.h");
	NativeToScriptTypeMap["Any"] = TypeMappingInformation(frameworkNs, "Any", ::ExportedClassTypeCategory::Class, "Utility/BsAny.h", "");

	// Parse C++ into an easy to read format
	const std::unique_ptr<BansheeCodeGeneratorFrontendActionFactory> factory = std::unique_ptr<BansheeCodeGeneratorFrontendActionFactory>(new BansheeCodeGeneratorFrontendActionFactory);
	int output = Tool.run(factory.get());

	CommentParser& commentParser = factory->GetCommentParser();
	ParserUtility::PostProcessFileInfos(commentParser);

	bool genEditor = GenerateEditorOption.getValue();

	// Generate code
	generateAll(
		OutputCppEngineOption.getValue(), 
		OutputCppEditorOption.getValue(),
		OutputCSEngineOption.getValue(),
		OutputCSEditorOption.getValue(),
		genEditor);

	//system("pause");
	return output;
}

