#include "fitz.h"
#include "mupdf.h"

struct stuff
{
	fz_obj *resources;
	fz_obj *mediabox;
	fz_obj *cropbox;
	fz_obj *rotate;
};

static fz_error
getpagecount(pdf_xref *xref, fz_obj *node, int *pagesp)
{
	fz_error error;
	fz_obj *type;
	fz_obj *kids;
	fz_obj *count;
	char *typestr;
	int pages = 0;
	int i;

	if (fz_isnull(node))
		return fz_throw("pagetree node is missing");

	type = fz_dictgets(node, "Type");
	kids = fz_dictgets(node, "Kids");
	count = fz_dictgets(node, "Count");

	if (!type)
	{
		fz_warn("pagetree node (%d %d R) lacks required type", fz_tonum(node), fz_togen(node));

		kids = fz_dictgets(node, "Kids");
		if (kids)
		{
			fz_warn("guessing it may be a pagetree node, continuing...");
			typestr = "Pages";
		}
		else
		{
			fz_warn("guessing it may be a page, continuing...");
			typestr = "Page";
		}
	}
	else
		typestr = fz_toname(type);

	if (!strcmp(typestr, "Page"))
		(*pagesp)++;

	else if (!strcmp(typestr, "Pages"))
	{
		if (!fz_isarray(kids))
			return fz_throw("page tree contains no pages");

		pdf_logpage("subtree (%d %d R) {\n", fz_tonum(node), fz_togen(node));

		for (i = 0; i < fz_arraylen(kids); i++)
		{
			fz_obj *obj = fz_arrayget(kids, i);

			/* prevent infinite recursion possible in maliciously crafted PDFs */
			if (obj == node)
				return fz_throw("corrupted pdf file");

			error = getpagecount(xref, obj, &pages);
			if (error)
				return fz_rethrow(error, "cannot load pagesubtree (%d %d R)", fz_tonum(obj), fz_togen(obj));
		}

		if (pages != fz_toint(count))
		{
			fz_warn("page tree node contains incorrect number of page, continuing...");

			error = fz_newint(&count, pages);
			if (!error)
			{
				error = fz_dictputs(node, "Count", count);
				fz_dropobj(count);
			}
			if (error)
				return fz_rethrow(error, "cannot correct wrong page count");
		}

		pdf_logpage("%d pages\n", pages);

		(*pagesp) += pages;

		pdf_logpage("}\n");
	}

	return fz_okay;
}

fz_error
pdf_getpagecount(pdf_xref *xref, int *pagesp)
{
	fz_error error;
	fz_obj *ref;
	fz_obj *catalog;
	fz_obj *pages;

	ref = fz_dictgets(xref->trailer, "Root");
	catalog = fz_resolveindirect(ref);

	pages = fz_dictgets(catalog, "Pages");
	pdf_logpage("determining page count (%d %d R) {\n", fz_tonum(pages), fz_togen(pages));

	*pagesp = 0;
	error = getpagecount(xref, pages, pagesp);
	if (error)
		return fz_rethrow(error, "cannot determine page count");

	pdf_logpage("}\n");

	return fz_okay;
}

static fz_error
getpageobject(pdf_xref *xref, struct stuff inherit, fz_obj *node, int *pagesp, int pageno, fz_obj **pagep)
{
	fz_error error;
	char *typestr;
	fz_obj *type;
	fz_obj *kids;
	fz_obj *count;
	fz_obj *inh;
	int i;

	if (fz_isnull(node))
		return fz_throw("pagetree node is missing");

	type = fz_dictgets(node, "Type");
	kids = fz_dictgets(node, "Kids");
	count = fz_dictgets(node, "Count");

	if (!type)
	{
		fz_warn("pagetree node (%d %d R) lacks required type", fz_tonum(node), fz_togen(node));

		kids = fz_dictgets(node, "Kids");
		if (kids)
		{
			fz_warn("guessing it may be a pagetree node, continuing...");
			typestr = "Pages";
		}
		else
		{
			fz_warn("guessing it may be a page, continuing...");
			typestr = "Page";
		}
	}
	else
		typestr = fz_toname(type);

	if (!strcmp(typestr, "Page"))
	{
		(*pagesp)++;
		if (*pagesp == pageno)
		{
			pdf_logpage("page %d (%d %d R)\n", *pagesp, fz_tonum(node), fz_togen(node));

			if (inherit.resources && !fz_dictgets(node, "Resources"))
			{
				pdf_logpage("inherited resources\n");
				error = fz_dictputs(node, "Resources", inherit.resources);
				if (error)
					return fz_rethrow(error, "cannot inherit page tree resources");
			}

			if (inherit.mediabox && !fz_dictgets(node, "MediaBox"))
			{
				pdf_logpage("inherit mediabox\n");
				error = fz_dictputs(node, "MediaBox", inherit.mediabox);
				if (error)
					return fz_rethrow(error, "cannot inherit page tree mediabox");
			}

			if (inherit.cropbox && !fz_dictgets(node, "CropBox"))
			{
				pdf_logpage("inherit cropbox\n");
				error = fz_dictputs(node, "CropBox", inherit.cropbox);
				if (error)
					return fz_rethrow(error, "cannot inherit page tree cropbox");
			}

			if (inherit.rotate && !fz_dictgets(node, "Rotate"))
			{
				pdf_logpage("inherit rotate\n");
				error = fz_dictputs(node, "Rotate", inherit.rotate);
				if (error)
					return fz_rethrow(error, "cannot inherit page tree rotate");
			}

			*pagep = node;
		}
	}

	else if (!strcmp(typestr, "Pages"))
	{
		if (!fz_isarray(kids))
			return fz_throw("page tree contains no pages");

		if (*pagesp + fz_toint(count) < pageno)
		{
			(*pagesp) += fz_toint(count);
			return fz_okay;
		}

		inh = fz_dictgets(node, "Resources");
		if (inh) inherit.resources = inh;

		inh = fz_dictgets(node, "MediaBox");
		if (inh) inherit.mediabox = inh;

		inh = fz_dictgets(node, "CropBox");
		if (inh) inherit.cropbox = inh;

		inh = fz_dictgets(node, "Rotate");
		if (inh) inherit.rotate = inh;

		pdf_logpage("subtree (%d %d R) {\n", fz_tonum(node), fz_togen(node));

		for (i = 0; !(*pagep) && i < fz_arraylen(kids); i++)
		{
			fz_obj *obj = fz_arrayget(kids, i);

			/* prevent infinite recursion possible in maliciously crafted PDFs */
			if (obj == node)
				return fz_throw("corrupted pdf file");

			error = getpageobject(xref, inherit, obj, pagesp, pageno, pagep);
			if (error)
				return fz_rethrow(error, "cannot load pagesubtree (%d %d R)", fz_tonum(obj), fz_togen(obj));
		}

		pdf_logpage("}\n");
	}

	return fz_okay;
}

fz_error
pdf_getpageobject(pdf_xref *xref, int pageno, fz_obj **pagep)
{
	fz_error error;
	struct stuff inherit;
	fz_obj *ref;
	fz_obj *catalog;
	fz_obj *pages;
	int count;

	// TODO: this is only to offset a regression in mupdf from
	//http://darcs.ghostscript.com/darcsweb.cgi?r=mupdf;a=commitdiff;h=20090709000319-86a4e-46304c4ff8ce491bfe1fced7fd46115dcfba940a.gz
	pageno += 1;

	inherit.resources = nil;
	inherit.mediabox = nil;
	inherit.cropbox = nil;
	inherit.rotate = nil;

	ref = fz_dictgets(xref->trailer, "Root");
	catalog = fz_resolveindirect(ref);

	pages = fz_dictgets(catalog, "Pages");
	pdf_logpage("get page %d (%d %d R) {\n", pageno, fz_tonum(pages), fz_togen(pages));

	*pagep = nil;
	count = 0;
	error = getpageobject(xref, inherit, pages, &count, pageno, pagep);
	if (error)
		return fz_rethrow(error, "cannot find page %d", pageno);

	pdf_logpage("}\n");

	return fz_okay;
}

static fz_error
findpageobject(pdf_xref *xref, fz_obj *node, fz_obj *page, int *pagenop, int *foundp)
{
	fz_error error;
	char *typestr;
	fz_obj *type;
	fz_obj *kids;
	int i;

	if (fz_isnull(node))
		return fz_throw("pagetree node is missing");

	type = fz_dictgets(node, "Type");
	kids = fz_dictgets(node, "Kids");

	if (!type)
	{
		fz_warn("pagetree node (%d %d R) lacks required type", fz_tonum(node), fz_togen(node));

		kids = fz_dictgets(node, "Kids");
		if (kids)
		{
			fz_warn("guessing it may be a pagetree node, continuing...");
			typestr = "Pages";
		}
		else
		{
			fz_warn("guessing it may be a page, continuing...");
			typestr = "Page";
		}
	}
	else
		typestr = fz_toname(type);

	if (!strcmp(typestr, "Page"))
	{
		(*pagenop)++;
		if (fz_tonum(node) == fz_tonum(page))
		{
			pdf_logpage("page %d (%d %d R)\n", *pagenop, fz_tonum(node), fz_togen(node));
			*foundp = 1;
		}
	}

	else if (!strcmp(typestr, "Pages"))
	{
		if (!fz_isarray(kids))
			return fz_throw("page tree contains no pages");

		pdf_logpage("subtree (%d %d R) {\n", fz_tonum(node), fz_togen(node));

		for (i = 0; !(*foundp) && i < fz_arraylen(kids); i++)
		{
			fz_obj *obj = fz_arrayget(kids, i);

			/* prevent infinite recursion possible in maliciously crafted PDFs */
			if (obj == node)
				return fz_throw("corrupted pdf file");

			error = findpageobject(xref, obj, page, pagenop, foundp);
			if (error)
				return fz_rethrow(error, "cannot load pagesubtree (%d %d R)", fz_tonum(obj), fz_togen(obj));
		}

		pdf_logpage("}\n");
	}

	return fz_okay;
}

fz_error
pdf_findpageobject(pdf_xref *xref, fz_obj *page, int *pagenop)
{
	fz_error error;
	fz_obj *ref;
	fz_obj *catalog;
	fz_obj *pages;
	int found;

	ref = fz_dictgets(xref->trailer, "Root");
	catalog = fz_resolveindirect(ref);

	pages = fz_dictgets(catalog, "Pages");
	pdf_logpage("find page object (%d %d R) (%d %d R) {\n", fz_tonum(page), fz_togen(page), fz_tonum(pages), fz_togen(pages));

	*pagenop = 0;
	found = 0;
	error = findpageobject(xref, pages, page, pagenop, &found);
	if (error)
		return fz_rethrow(error, "cannot find page object (%d %d R)", fz_tonum(page), fz_togen(page));

	pdf_logpage("}\n");

	if (!found)
		return fz_throw("cannot find page object (%d %d R)", fz_tonum(page), fz_togen(page));

	return fz_okay;
}

