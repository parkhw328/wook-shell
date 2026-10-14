#include "file_drag.hpp"
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <cstring>
#include <stdexcept>

namespace fileDrag {
#ifdef WOOK_UI_TEST
std::function<HRESULT(IDataObject *)> probe;
#endif
namespace {
CLIPFORMAT descriptor() { return (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW); }
CLIPFORMAT contents() { return (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILECONTENTS); }
CLIPFORMAT effect() { return (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT); }
HRESULT copyMedium(const void *data, SIZE_T size, STGMEDIUM *medium) {
    auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    if (!memory) return E_OUTOFMEMORY;
    auto buffer = GlobalLock(memory);
    if (!buffer) { GlobalFree(memory); return E_OUTOFMEMORY; }
    memcpy(buffer, data, size); GlobalUnlock(memory);
    medium->tymed = TYMED_HGLOBAL; medium->hGlobal = memory; medium->pUnkForRelease = nullptr;
    return S_OK;
}
class Data final : public IDataObject {
    LONG refs = 1;
public:
    std::vector<std::wstring> names;
    std::vector<File> files;
    Read read;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDataObject) return E_NOINTERFACE;
        *out = static_cast<IDataObject *>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { auto n=InterlockedDecrement(&refs); if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC *f) override {
        if (!f) return E_POINTER;
        if (f->dwAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
        if (f->cfFormat == contents() && !files.empty()) {
            if (!(f->tymed & TYMED_ISTREAM)) return DV_E_TYMED;
            return f->lindex == -1 || (f->lindex >= 0 && (size_t)f->lindex < files.size()) ? S_OK : DV_E_LINDEX;
        }
        if (f->lindex != -1) return DV_E_LINDEX;
        if (!(f->tymed & TYMED_HGLOBAL)) return DV_E_TYMED;
        if (f->cfFormat == effect() || (f->cfFormat == CF_HDROP && !names.empty()) ||
            (f->cfFormat == descriptor() && !files.empty())) return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC *f, STGMEDIUM *m) override {
        if (!m) return E_POINTER;
        *m = {};
        auto result=QueryGetData(f); if (FAILED(result)) return result;
        try {
            if (f->cfFormat == contents()) {
                if(f->lindex<0)return DV_E_LINDEX;
                if (!read) return STG_E_REVERTED;
                IStream *stream=nullptr; result=read((size_t)f->lindex,&stream);
                if (FAILED(result)) return result;
                m->tymed=TYMED_ISTREAM; m->pstm=stream; return S_OK;
            }
            if (f->cfFormat == effect()) { DWORD copy=DROPEFFECT_COPY; return copyMedium(&copy,sizeof(copy),m); }
            if (f->cfFormat == CF_HDROP) {
                size_t chars=1; for (const auto &name:names) chars+=name.size()+1;
                std::vector<BYTE> buffer(sizeof(DROPFILES)+chars*sizeof(wchar_t));
                auto drop=(DROPFILES *)buffer.data(); drop->pFiles=sizeof(DROPFILES); drop->fWide=TRUE;
                auto next=(wchar_t *)(buffer.data()+sizeof(DROPFILES));
                for (const auto &name:names) { memcpy(next,name.c_str(),(name.size()+1)*sizeof(wchar_t)); next+=name.size()+1; }
                return copyMedium(buffer.data(),buffer.size(),m);
            }
            std::vector<BYTE> buffer(FIELD_OFFSET(FILEGROUPDESCRIPTORW,fgd)+files.size()*sizeof(FILEDESCRIPTORW));
            auto group=(FILEGROUPDESCRIPTORW *)buffer.data(); group->cItems=(UINT)files.size();
            for (size_t i=0;i<files.size();++i) {
                auto &d=group->fgd[i]; d.dwFlags=FD_ATTRIBUTES|FD_FILESIZE|FD_PROGRESSUI;
                d.dwFileAttributes=FILE_ATTRIBUTE_NORMAL;
                d.nFileSizeHigh=(DWORD)(files[i].size>>32); d.nFileSizeLow=(DWORD)files[i].size;
                if (files[i].name.size()>=MAX_PATH) return DV_E_FORMATETC;
                memcpy(d.cFileName,files[i].name.c_str(),(files[i].name.size()+1)*sizeof(wchar_t));
            }
            return copyMedium(buffer.data(),buffer.size(),m);
        } catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC *, STGMEDIUM *) override { return DATA_E_FORMATETC; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC *,FORMATETC *out) override { if(out)out->ptd=nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC *,STGMEDIUM *,BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction,IEnumFORMATETC **out) override {
        if (direction!=DATADIR_GET) return E_NOTIMPL;
        FORMATETC formats[3]{}; UINT count=0;
        if (!names.empty()) formats[count++]={CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
        if (!files.empty()) {
            formats[count++]={descriptor(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
            formats[count++]={contents(),nullptr,DVASPECT_CONTENT,-1,TYMED_ISTREAM};
        }
        formats[count++]={effect(),nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};
        return SHCreateStdEnumFmtEtc(count,formats,out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC *,DWORD,IAdviseSink *,DWORD *) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA **) override { return OLE_E_ADVISENOTSUPPORTED; }
};
class Source final : public IDropSource {
    LONG refs=1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_IDropSource) return E_NOINTERFACE;
        *out=static_cast<IDropSource *>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { auto n=InterlockedDecrement(&refs); if(!n)delete this; return n; }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys) override {
        if (escape || (keys&MK_RBUTTON)) return DRAGDROP_S_CANCEL;
        return keys&MK_LBUTTON ? S_OK : DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};
class Target final : public IDropTarget {
    LONG refs=1;
    bool allowed=false;
    Accept accept; fileDrag::Drop drop;
public:
    Target(Accept a,fileDrag::Drop d):accept(std::move(a)),drop(std::move(d)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_IDropTarget) return E_NOINTERFACE;
        *out=static_cast<IDropTarget *>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { auto n=InterlockedDecrement(&refs); if(!n)delete this; return n; }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject *data,DWORD,POINTL,DWORD *effect) override {
        try { allowed=accept(data); } catch (...) { allowed=false; }
        *effect=allowed ? (*effect&DROPEFFECT_COPY) : DROPEFFECT_NONE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL,DWORD *effect) override { *effect=allowed?(*effect&DROPEFFECT_COPY):DROPEFFECT_NONE; return S_OK; }
    HRESULT STDMETHODCALLTYPE DragLeave() override { allowed=false; return S_OK; }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject *data,DWORD,POINTL,DWORD *effect) override {
        bool copied=false;
        try { if ((*effect&DROPEFFECT_COPY) && accept(data)) copied=drop(data); } catch (...) {}
        *effect=copied?DROPEFFECT_COPY:DROPEFFECT_NONE; allowed=false; return S_OK;
    }
};
// Explorer may retain a stream after DoDragDrop returns. Keep its private files alive.
class Stream final : public IStream {
    LONG refs=1;
    IStream *inner;
    std::shared_ptr<void> lifetime;
public:
    Stream(IStream *s,std::shared_ptr<void> l):inner(s),lifetime(std::move(l)){}
    ~Stream() { inner->Release(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if(iid!=IID_IUnknown&&iid!=IID_ISequentialStream&&iid!=IID_IStream)return E_NOINTERFACE;
        *out=static_cast<IStream *>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override { auto n=InterlockedDecrement(&refs);if(!n)delete this;return n; }
    HRESULT STDMETHODCALLTYPE Read(void *p,ULONG n,ULONG *read) override { return inner->Read(p,n,read); }
    HRESULT STDMETHODCALLTYPE Write(const void *,ULONG,ULONG *) override { return STG_E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER n,DWORD origin,ULARGE_INTEGER *pos) override {return inner->Seek(n,origin,pos);}
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override {return STG_E_ACCESSDENIED;}
    HRESULT STDMETHODCALLTYPE CopyTo(IStream *s,ULARGE_INTEGER n,ULARGE_INTEGER *r,ULARGE_INTEGER *w) override {return inner->CopyTo(s,n,r,w);}
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE Revert() override {return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override {return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER,ULARGE_INTEGER,DWORD) override {return STG_E_INVALIDFUNCTION;}
    HRESULT STDMETHODCALLTYPE Stat(STATSTG *s,DWORD flags) override {return inner->Stat(s,flags);}
    HRESULT STDMETHODCALLTYPE Clone(IStream **out) override {
        if(!out)return E_POINTER;*out=nullptr;IStream *copy=nullptr;
        auto hr=inner->Clone(&copy);if(FAILED(hr))return hr;
        try {*out=new Stream(copy,lifetime);return S_OK;}catch(...){copy->Release();return E_OUTOFMEMORY;}
    }
};
}
IDataObject *local(const std::vector<std::wstring> &paths) { auto d=std::make_unique<Data>();d->names=paths;return d.release(); }
IDataObject *remote(const std::vector<File> &files,Read read) { auto d=std::make_unique<Data>();d->files=files;d->read=std::move(read);return d.release(); }
HRESULT run(IDataObject *data) {
#ifdef WOOK_UI_TEST
    if(probe) {auto result=probe(data);static_cast<Data *>(data)->read={};return result;}
#endif
    auto source=new Source();DWORD effect=0;auto hr=DoDragDrop(data,source,DROPEFFECT_COPY,&effect);
    source->Release();static_cast<Data *>(data)->read={};return hr;
}
bool hasPaths(IDataObject *data) { FORMATETC f{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};return data && data->QueryGetData(&f)==S_OK; }
std::vector<std::wstring> paths(IDataObject *data) {
    FORMATETC f{CF_HDROP,nullptr,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM m{};
    if(FAILED(data->GetData(&f,&m)))throw std::runtime_error("Cannot read the dropped files.");
    struct Release {STGMEDIUM &m;~Release(){ReleaseStgMedium(&m);}} release{m};
    if(m.tymed!=TYMED_HGLOBAL || !m.hGlobal)throw std::runtime_error("Invalid file drop.");
    auto count=DragQueryFileW((HDROP)m.hGlobal,0xffffffff,nullptr,0);
    if(count>100000)throw std::runtime_error("Too many files in one drop.");
    std::vector<std::wstring> result;
    for(UINT i=0;i<count;++i){auto size=DragQueryFileW((HDROP)m.hGlobal,i,nullptr,0);
        if(!size||size>32767)throw std::runtime_error("Invalid dropped filename.");
        std::wstring name(size+1,L'\0');DragQueryFileW((HDROP)m.hGlobal,i,name.data(),size+1);name.resize(size);result.push_back(std::move(name));}
    return result;
}
IDropTarget *target(HWND window,Accept accept,Drop drop) {
    auto t=new Target(std::move(accept),std::move(drop));auto hr=RegisterDragDrop(window,t);
    if(FAILED(hr)){t->Release();throw std::runtime_error("Cannot enable file drops.");}return t;
}
HRESULT openStream(const std::wstring &path,std::shared_ptr<void> lifetime,IStream **stream) {
    if(!stream)return E_POINTER;*stream=nullptr;IStream *inner=nullptr;
    auto hr=SHCreateStreamOnFileEx(path.c_str(),STGM_READ|STGM_SHARE_DENY_WRITE,FILE_ATTRIBUTE_NORMAL,FALSE,nullptr,&inner);
    if(FAILED(hr))return hr;
    try{*stream=new Stream(inner,std::move(lifetime));return S_OK;}catch(...){inner->Release();return E_OUTOFMEMORY;}
}
}
