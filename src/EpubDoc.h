/* Copyright 2012 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#ifndef EpubDoc_h
#define EpubDoc_h

#include "EbookBase.h"
#include "Scoped.h"
#include "Vec.h"
#include "ZipUtil.h"

struct ImageData2 {
    ImageData base;
    char *  id;  // path by which content refers to this image
    size_t  idx; // document specific index at which to find this image
};

class EpubDoc {
    ZipFile zip;
    str::Str<char> htmlData;
    Vec<ImageData2> images;
    Vec<const char *> props;
    ScopedMem<TCHAR> tocPath;
    // NULL if initialized from IStream
    TCHAR *filePath;

    bool Load();
    void ParseMetadata(const char *content);

    static bool VerifyEpub(ZipFile& zip);

public:
    EpubDoc(const TCHAR *fileName);
    EpubDoc(IStream *stream);
    ~EpubDoc();

    const char *GetTextData(size_t *lenOut);
    ImageData *GetImageData(const char *id, const char *pagePath);

    TCHAR *GetProperty(const char *name);

    bool HasToc() const;
    bool ParseToc(EbookTocVisitor *visitor);

    TCHAR *GetFilePath() const { return filePath; }

    static bool IsSupportedFile(const TCHAR *fileName, bool sniff);
    static EpubDoc *CreateFromFile(const TCHAR *fileName);
    static EpubDoc *CreateFromStream(IStream *stream);
};

class HtmlPullParser;
struct HtmlToken;

class Fb2Doc {
    ScopedMem<TCHAR> fileName;
    str::Str<char> xmlData;
    Vec<ImageData2> images;
    ScopedMem<TCHAR> docTitle;
    ScopedMem<TCHAR> docAuthor;
    ScopedMem<char> hrefName;

    bool isZipped;
    bool hasToc;

    bool Load();
    void ExtractImage(HtmlPullParser *parser, HtmlToken *tok);

public:
    Fb2Doc(const TCHAR *fileName);
    ~Fb2Doc();


    const char *GetTextData(size_t *lenOut);
    ImageData *GetImageData(const char *id);

    TCHAR *GetProperty(const char *name);
    const char *GetHrefName();
    bool IsZipped();

    static bool IsSupportedFile(const TCHAR *fileName, bool sniff);
    static Fb2Doc *CreateFromFile(const TCHAR *fileName);
};

char *NormalizeURL(const char *url, const char *base);

#endif
