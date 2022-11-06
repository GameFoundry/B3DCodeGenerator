#pragma once
#include "B3DCommon.h"

/** Handles generation of XML comments. */
class XMLCommentGenerator
{
public:
	/**
	 * Generates a complete XML comment for the specified entry, corresponding to a XML 'summary' block with a brief and
	 * optional parameter and return value documentation.
	 *
	 * @param commentEntry		Entry to generate the comment for.
	 * @param indent			Indentation to use when writing the comment.
	 * @return					Generated XML comment.
	 */
	static std::string GenerateXMLComment(const CommentEntry& commentEntry, const std::string& indent);

	/*** Generates a single paragraph for XML documentation. References are automatically converted into XML references. */
	static std::string GenerateXMLCommentParagraph(const CommentParagraph& commentTextEntry);

	/*** Generates for XML documentation for multiple paragraph. References are automatically converted into XML references. */
	static std::string GenerateXMLCommentParagraph(const SmallVector<CommentParagraph, 2>& commentTextEntries);
};
