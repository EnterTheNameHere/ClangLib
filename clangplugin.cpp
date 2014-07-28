/*
 * A clang based plugin
 */

#include <sdk.h>

#include "clangplugin.h"

#include <compilercommandgenerator.h>
#include <cbstyledtextctrl.h>
#include <editor_hooks.h>

#include <wx/tokenzr.h>

#ifndef CB_PRECOMP
    #include <cbeditor.h>
    #include <cbproject.h>
    #include <compilerfactory.h>
    #include <configmanager.h>
    #include <editorcolourset.h>
    #include <editormanager.h>
    #include <logmanager.h>
    #include <macrosmanager.h>
    #include <projectfile.h>
    #include <projectmanager.h>

    #include <algorithm>
    #include <wx/dir.h>
#endif // CB_PRECOMP

// this auto-registers the plugin
namespace
{
    PluginRegistrant<ClangPlugin> reg(wxT("ClangLib"));
}

static const wxString g_InvalidStr(wxT("invalid"));
const int idEdOpenTimer     = wxNewId();
const int idReparseTimer    = wxNewId();
const int idDiagnosticTimer = wxNewId();

const int idGotoDeclaration = wxNewId();

// milliseconds
#define ED_OPEN_DELAY 1000
#define ED_ACTIVATE_DELAY 150
#define REPARSE_DELAY 900
#define DIAGNOSTIC_DELAY 3000

ClangPlugin::ClangPlugin() :
    m_Proxy(m_Database, m_CppKeywords),
    m_ImageList(16, 16),
    m_EdOpenTimer(this, idEdOpenTimer),
    m_ReparseTimer(this, idReparseTimer),
    m_DiagnosticTimer(this, idDiagnosticTimer),
    m_pLastEditor(nullptr),
    m_TranslUnitId(wxNOT_FOUND)
{
    if (!Manager::LoadResource(_T("clanglib.zip")))
        NotifyMissingFile(_T("clanglib.zip"));
}

ClangPlugin::~ClangPlugin()
{
}

void ClangPlugin::OnAttach()
{
    wxBitmap bmp;
    wxString prefix = ConfigManager::GetDataFolder() + wxT("/images/codecompletion/");
    // bitmaps must be added by order of PARSER_IMG_* consts (which are also TokenCategory enums)
    bmp = cbLoadBitmap(prefix + wxT("class_folder.png"),        wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CLASS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("class.png"),               wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CLASS
    bmp = cbLoadBitmap(prefix + wxT("class_private.png"),       wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CLASS_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("class_protected.png"),     wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CLASS_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("class_public.png"),        wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CLASS_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("ctor_private.png"),        wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CTOR_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("ctor_protected.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CTOR_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("ctor_public.png"),         wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_CTOR_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("dtor_private.png"),        wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_DTOR_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("dtor_protected.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_DTOR_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("dtor_public.png"),         wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_DTOR_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("method_private.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_FUNC_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("method_protected.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_FUNC_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("method_public.png"),       wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_FUNC_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("var_private.png"),         wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_VAR_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("var_protected.png"),       wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_VAR_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("var_public.png"),          wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_VAR_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("macro_def.png"),           wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_DEF
    bmp = cbLoadBitmap(prefix + wxT("enum.png"),                wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUM
    bmp = cbLoadBitmap(prefix + wxT("enum_private.png"),        wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUM_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("enum_protected.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUM_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("enum_public.png"),         wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUM_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("enumerator.png"),          wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUMERATOR
    bmp = cbLoadBitmap(prefix + wxT("namespace.png"),           wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_NAMESPACE
    bmp = cbLoadBitmap(prefix + wxT("typedef.png"),             wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_TYPEDEF
    bmp = cbLoadBitmap(prefix + wxT("typedef_private.png"),     wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_TYPEDEF_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("typedef_protected.png"),   wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_TYPEDEF_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("typedef_public.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_TYPEDEF_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("symbols_folder.png"),      wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_SYMBOLS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("vars_folder.png"),         wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_VARS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("funcs_folder.png"),        wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_FUNCS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("enums_folder.png"),        wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_ENUMS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("macro_def_folder.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_DEF_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("others_folder.png"),       wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_OTHERS_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("typedefs_folder.png"),     wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_TYPEDEF_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("macro_use.png"),           wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_USE
    bmp = cbLoadBitmap(prefix + wxT("macro_use_private.png"),   wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_USE_PRIVATE
    bmp = cbLoadBitmap(prefix + wxT("macro_use_protected.png"), wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_USE_PROTECTED
    bmp = cbLoadBitmap(prefix + wxT("macro_use_public.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_USE_PUBLIC
    bmp = cbLoadBitmap(prefix + wxT("macro_use_folder.png"),    wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // PARSER_IMG_MACRO_USE_FOLDER
    bmp = cbLoadBitmap(prefix + wxT("cpp_lang.png"),            wxBITMAP_TYPE_PNG);
    m_ImageList.Add(bmp); // tcLangKeyword

    EditorColourSet* theme = Manager::Get()->GetEditorManager()->GetColourSet();
    wxStringTokenizer tokenizer(theme->GetKeywords(theme->GetHighlightLanguage(wxT("C/C++")), 0));
    while (tokenizer.HasMoreTokens())
        m_CppKeywords.push_back(tokenizer.GetNextToken());
    std::sort(m_CppKeywords.begin(), m_CppKeywords.end());
    wxStringVec(m_CppKeywords).swap(m_CppKeywords);

    typedef cbEventFunctor<ClangPlugin, CodeBlocksEvent> ClEvent;
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_OPEN,      new ClEvent(this, &ClangPlugin::OnEditorOpen));
    Manager::Get()->RegisterEventSink(cbEVT_EDITOR_ACTIVATED, new ClEvent(this, &ClangPlugin::OnEditorActivate));
    Connect(idEdOpenTimer,     wxEVT_TIMER, wxTimerEventHandler(ClangPlugin::OnTimer));
    Connect(idReparseTimer,    wxEVT_TIMER, wxTimerEventHandler(ClangPlugin::OnTimer));
    Connect(idDiagnosticTimer, wxEVT_TIMER, wxTimerEventHandler(ClangPlugin::OnTimer));
    Connect(idGotoDeclaration, wxEVT_COMMAND_MENU_SELECTED, /*wxMenuEventHandler*/wxCommandEventHandler(ClangPlugin::OnGotoDeclaration), nullptr, this);
    m_EditorHookId = EditorHooks::RegisterHook(new EditorHooks::HookFunctor<ClangPlugin>(this, &ClangPlugin::OnEditorHook));
}

void ClangPlugin::OnRelease(cb_unused bool appShutDown)
{
    EditorHooks::UnregisterHook(m_EditorHookId);
    Disconnect(idGotoDeclaration);
    Disconnect(idDiagnosticTimer);
    Disconnect(idReparseTimer);
    Disconnect(idEdOpenTimer);
    Manager::Get()->RemoveAllEventSinksFor(this);
    m_ImageList.RemoveAll();
}

ClangPlugin::CCProviderStatus ClangPlugin::GetProviderStatusFor(cbEditor* ed)
{
    if (ed->GetLanguage() == ed->GetColourSet()->GetHighlightLanguage(wxT("C/C++")))
        return ccpsActive;
    return ccpsInactive;
}

struct PrioritySorter
{
    bool operator()(const ClangPlugin::CCToken& a, const ClangPlugin::CCToken& b)
    {
        return a.weight < b.weight;
    }
};

static wxString GetActualName(const wxString& name)
{
    const int idx = name.Find(wxT(':'));
    if (idx == wxNOT_FOUND)
        return name;
    return name.Mid(0, idx);
}

std::vector<ClangPlugin::CCToken> ClangPlugin::GetAutocompList(bool isAuto, cbEditor* ed, int& tknStart, int& tknEnd)
{
    std::vector<CCToken> tokens;
    if (ed != m_pLastEditor)
    {
        m_TranslUnitId = m_Proxy.GetTranslationUnitId(ed->GetFilename());
        m_pLastEditor = ed;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
    {
        Manager::Get()->GetLogManager()->LogWarning(wxT("ClangLib: m_TranslUnitId == wxNOT_FOUND, cannot complete in file ") + ed->GetFilename());
        return tokens;
    }

    cbStyledTextCtrl* stc = ed->GetControl();
    const int style = stc->GetStyleAt(tknEnd);
    const wxChar curChar = stc->GetCharAt(tknEnd - 1);
    if (isAuto) // filter illogical cases of auto-launch
    {
        if (   (   curChar == wxT(':') // scope operator
                && stc->GetCharAt(tknEnd - 2) != wxT(':') )
            || (   curChar == wxT('>') // '->'
                && stc->GetCharAt(tknEnd - 2) != wxT('-') )
            || (   wxString(wxT("<\"/")).Find(curChar) != wxNOT_FOUND // #include directive (TODO: enumerate completable include files)
                && !stc->IsPreprocessor(style) ) )
        {
            return tokens;
        }
    }

    std::vector<ClToken> tknResults;
    const int line = stc->LineFromPosition(tknStart);
    std::map<wxString, wxString> unsavedFiles;
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
    {
        cbEditor* editor = edMgr->GetBuiltinEditor(i);
        if (editor && editor->GetModified())
            unsavedFiles.insert(std::make_pair(editor->GetFilename(), editor->GetControl()->GetText()));
    }
    const int lnStart = stc->PositionFromLine(line);
    int column = tknStart - lnStart;
    for (; column > 0; --column)
    {
        if (   !wxIsspace(stc->GetCharAt(lnStart + column - 1))
            || (column != 1 && !wxIsspace(stc->GetCharAt(lnStart + column - 2))) )
        {
            break;
        }
    }
    m_Proxy.CodeCompleteAt(isAuto, ed->GetFilename(), line + 1, column + 1, m_TranslUnitId, unsavedFiles, tknResults);
    const wxString& prefix = stc->GetTextRange(tknStart, tknEnd).Lower();
    bool includeCtors = true; // sometimes we get a lot of these
    for (int i = tknStart - 1; i > 0; --i)
    {
        wxChar chr = stc->GetCharAt(i);
        if (!wxIsspace(chr))
        {
            if (chr == wxT(';') || chr == wxT('}')) // last non-whitespace character
                includeCtors = false; // filter out ctors (they are unlikely to be wanted in this situation)
            break;
        }
    }
    if (prefix.Length() > 3) // larger context, match the prefix at any point in the token
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
             tknIt != tknResults.end(); ++tknIt)
        {
            if (tknIt->name.Lower().Find(prefix) != wxNOT_FOUND && (includeCtors || tknIt->category != tcCtorPublic))
                tokens.push_back(CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }
    else if (prefix.IsEmpty())
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
             tknIt != tknResults.end(); ++tknIt)
        {
            if (!tknIt->name.StartsWith(wxT("operator")) && (includeCtors || tknIt->category != tcCtorPublic)) // it is rather unlikely for an operator to be the desired completion
                tokens.push_back(CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }
    else // smaller context, only allow matches of the prefix at the beginning of the token
    {
        for (std::vector<ClToken>::const_iterator tknIt = tknResults.begin();
             tknIt != tknResults.end(); ++tknIt)
        {
            if (tknIt->name.Lower().StartsWith(prefix) && (includeCtors || tknIt->category != tcCtorPublic))
                tokens.push_back(CCToken(tknIt->id, tknIt->name, tknIt->name, tknIt->weight, tknIt->category));
        }
    }

    if (!tokens.empty())
    {
        if (prefix.IsEmpty() && tokens.size() > 1500) // reduce to give only top matches
        {
            std::partial_sort(tokens.begin(), tokens.begin() + 1000, tokens.end(), PrioritySorter());
            tokens.erase(tokens.begin() + 1000, tokens.end());
        }
        const int imgCount = m_ImageList.GetImageCount();
        for (int i = 0; i < imgCount; ++i)
            stc->RegisterImage(i, m_ImageList.GetBitmap(i));
        bool isPP = stc->GetLine(line).Strip(wxString::leading).StartsWith(wxT("#"));
        std::set<int> usedWeights;
        for (std::vector<CCToken>::iterator tknIt = tokens.begin();
             tknIt != tokens.end(); ++tknIt)
        {
            usedWeights.insert(tknIt->weight);
            switch (tknIt->category)
            {
                case tcNone:
                    if (isPP)
                        tknIt->category = tcMacroDef;
                    else if (std::binary_search(m_CppKeywords.begin(), m_CppKeywords.end(), GetActualName(tknIt->name)))
                        tknIt->category = tcLangKeyword;
                    break;

                case tcClass:
                case tcCtorPublic:
                case tcDtorPublic:
                case tcFuncPublic:
                case tcVarPublic:
                case tcEnum:
                case tcTypedef:
                    m_Proxy.RefineTokenType(m_TranslUnitId, tknIt->id, tknIt->category);
                    break;

                default:
                    break;
            }
        }
        // Clang sometimes gives many weight values, which can make completion more difficult
        // because results are less alphabetical. Use a compression map on the lower priority
        // values (higher numbers) to reduce the total number of weights used.
        if (usedWeights.size() > 3)
        {
            std::vector<int> weightsVec(usedWeights.begin(), usedWeights.end());
            std::map<int, int> weightCompr;
            weightCompr[weightsVec[0]] = weightsVec[0];
            weightCompr[weightsVec[1]] = weightsVec[1];
            int factor = (weightsVec.size() > 7 ? 3 : 2);
            for (size_t i = 2; i < weightsVec.size(); ++i)
                weightCompr[weightsVec[i]] = weightsVec[(i - 2) / factor + 2];
            for (std::vector<CCToken>::iterator tknIt = tokens.begin();
                 tknIt != tokens.end(); ++tknIt)
            {
                tknIt->weight = weightCompr[tknIt->weight];
            }
        }
    }

    return tokens;
}

wxString ClangPlugin::GetDocumentation(const CCToken& token)
{
    if (token.id >= 0)
        return m_Proxy.DocumentCCToken(m_TranslUnitId, token.id);
    return wxEmptyString;
}

std::vector<ClangPlugin::CCCallTip> ClangPlugin::GetCallTips(int pos, cb_unused int style, cbEditor* ed, int& argsPos)
{
    std::vector<CCCallTip> tips;
    if (ed != m_pLastEditor)
    {
        m_TranslUnitId = m_Proxy.GetTranslationUnitId(ed->GetFilename());
        m_pLastEditor = ed;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
        return tips;

    cbStyledTextCtrl* stc = ed->GetControl();

    int nest = 0;
    int commas = 0;

    while (--pos > 0)
    {
        const int curStyle = stc->GetStyleAt(pos);
        if (   stc->IsString(curStyle)
            || stc->IsCharacter(curStyle)
            || stc->IsComment(curStyle) )
        {
            continue;
        }

        const wxChar ch = stc->GetCharAt(pos);
        if (ch == wxT(';'))
            return tips; // error?
        else if (ch == wxT(','))
        {
            if (nest == 0)
                ++commas;
        }
        else if (ch == wxT(')'))
            --nest;
        else if (ch == wxT('('))
        {
            ++nest;
            if (nest > 0)
                break;
        }
    }
    while (--pos > 0)
    {
        if (   stc->GetCharAt(pos) <= wxT(' ')
            || stc->IsComment(stc->GetStyleAt(pos)) )
        {
            continue;
        }
        break;
    }
    argsPos = stc->WordEndPosition(pos, true);
    if (argsPos != m_LastCallTipPos)
    {
        m_LastCallTips.clear();
        const int line = stc->LineFromPosition(pos);
        const int column = pos - stc->PositionFromLine(line);
        const wxString& tknText = stc->GetTextRange(stc->WordStartPosition(pos, true), argsPos);
        if (!tknText.IsEmpty())
            m_Proxy.GetCallTipsAt(ed->GetFilename(), line + 1, column + 1, m_TranslUnitId, tknText, m_LastCallTips);
    }
    m_LastCallTipPos = argsPos;
    for (std::vector<wxStringVec>::const_iterator strVecItr = m_LastCallTips.begin(); strVecItr != m_LastCallTips.end(); ++strVecItr)
    {
        int strVecSz = strVecItr->size();
        if (commas != 0 && strVecSz < commas + 3)
            continue;
        wxString tip;
        int hlStart = wxSCI_INVALID_POSITION;
        int hlEnd = wxSCI_INVALID_POSITION;
        for (int i = 0; i < strVecSz; ++i)
        {
            if (i == commas + 1 && strVecSz > 2)
            {
                hlStart = tip.Length();
                hlEnd = hlStart + (*strVecItr)[i].Length();
            }
            tip += (*strVecItr)[i];
            if (i > 0 && i < (strVecSz - 2))
                tip += wxT(", ");
        }
        tips.push_back(CCCallTip(tip, hlStart, hlEnd));
    }

    return tips;
}

std::vector<ClangPlugin::CCToken> ClangPlugin::GetTokenAt(int pos, cbEditor* ed, cb_unused bool& allowCallTip)
{
    std::vector<CCToken> tokens;
    if (ed != m_pLastEditor)
    {
        m_TranslUnitId = m_Proxy.GetTranslationUnitId(ed->GetFilename());
        m_pLastEditor = ed;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
        return tokens;

    cbStyledTextCtrl* stc = ed->GetControl();
    if (stc->GetTextRange(pos - 1, pos + 1).Strip().IsEmpty())
        return tokens;
    const int line = stc->LineFromPosition(pos);
    const int column = pos - stc->PositionFromLine(line);
    wxStringVec names;
    m_Proxy.GetTokensAt(ed->GetFilename(), line + 1, column + 1, m_TranslUnitId, names);
    for (wxStringVec::const_iterator nmIt = names.begin(); nmIt != names.end(); ++nmIt)
        tokens.push_back(CCToken(-1, *nmIt));
    return tokens;
}

wxString ClangPlugin::OnDocumentationLink(cb_unused wxHtmlLinkEvent& event, cb_unused bool& dismissPopup)
{
    return wxEmptyString;
}

void ClangPlugin::DoAutocomplete(const CCToken& token, cbEditor* ed)
{
    wxString tknText = token.name;
    int idx = tknText.Find(wxT(':'));
    if (idx != wxNOT_FOUND)
        tknText.Truncate(idx);
    std::pair<int, int> offsets = std::make_pair(0, 0);
    cbStyledTextCtrl* stc = ed->GetControl();
    wxString suffix = m_Proxy.GetCCInsertSuffix(m_TranslUnitId, token.id, GetEOLStr(stc->GetEOLMode()) + ed->GetLineIndentString(stc->GetCurrentLine()), offsets);
    int pos = stc->GetCurrentPos();
    int startPos = std::min(stc->WordStartPosition(pos, true), std::min(stc->GetSelectionStart(), stc->GetSelectionEnd()));
    int moveToPos = startPos + tknText.Length();
    stc->SetTargetStart(startPos);
    int endPos = stc->WordEndPosition(pos, true);
    if (tknText.EndsWith(stc->GetTextRange(pos, endPos)))
    {
        if (!suffix.IsEmpty())
        {
            if ( static_cast<unsigned int>( stc->GetCharAt(endPos) ) == suffix[0].GetValue() )
            {
                if ( suffix.Length() != 2
                     || static_cast<unsigned int>( stc->GetCharAt(endPos + 1) ) != suffix[1].GetValue() )
                {
                    offsets = std::make_pair(1, 1);
                }
            }
            else
                tknText += suffix;
        }
    }
    else
    {
        endPos = pos;
        tknText += suffix;
    }
    stc->SetTargetEnd(endPos);

    stc->AutoCompCancel(); // so (wx)Scintilla does not insert the text as well

    if (stc->GetTextRange(startPos, endPos) != tknText)
        stc->ReplaceTarget(tknText);
    stc->SetSelectionVoid(moveToPos + offsets.first, moveToPos + offsets.second);
    stc->ChooseCaretX();
    if (token.category != tcLangKeyword && (offsets.first != offsets.second || offsets.first == 1))
    {
        CodeBlocksEvent evt(cbEVT_SHOW_CALL_TIP);
        Manager::Get()->ProcessEvent(evt);
    }
}

void ClangPlugin::BuildMenu(wxMenuBar* menuBar)
{
    int idx = menuBar->FindMenu(_("Sea&rch"));
    if (idx != wxNOT_FOUND)
    {
        menuBar->GetMenu(idx)->Append(idGotoDeclaration, _("Resolve token (clang)"));
    }
}

void ClangPlugin::BuildModuleMenu(const ModuleType type, wxMenu* menu, cb_unused const FileTreeData* data)
{
    if (type != mtEditorManager)
        return;
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed)
        return;
    if (ed != m_pLastEditor)
    {
        m_TranslUnitId = m_Proxy.GetTranslationUnitId(ed->GetFilename());
        m_pLastEditor = ed;
    }
    if (m_TranslUnitId == wxNOT_FOUND)
        return;
    cbStyledTextCtrl* stc = ed->GetControl();
    const int pos = stc->GetCurrentPos();
    if (stc->GetTextRange(pos - 1, pos + 1).Strip().IsEmpty())
        return;
    menu->Insert(0, idGotoDeclaration, _("Resolve token (clang)"));
}

void ClangPlugin::OnEditorOpen(CodeBlocksEvent& event)
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed)
        m_EdOpenTimer.Start(ED_OPEN_DELAY, wxTIMER_ONE_SHOT);
    event.Skip();
}

void ClangPlugin::OnEditorActivate(CodeBlocksEvent& event)
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinEditor(event.GetEditor());
    if (ed && !m_EdOpenTimer.IsRunning())
        m_EdOpenTimer.Start(ED_ACTIVATE_DELAY, wxTIMER_ONE_SHOT);
    event.Skip();
}

void ClangPlugin::OnGotoDeclaration(wxCommandEvent& WXUNUSED(event))
{
    cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
    if (!ed || m_TranslUnitId == wxNOT_FOUND)
        return;
    cbStyledTextCtrl* stc = ed->GetControl();
    const int pos = stc->GetCurrentPos();
    wxString filename = ed->GetFilename();
    int line = stc->LineFromPosition(pos);
    int column = pos - stc->PositionFromLine(line) + 1;
    if (stc->GetLine(line).StartsWith(wxT("#include")))
        column = 2;
    ++line;
    m_Proxy.ResolveTokenAt(filename, line, column, m_TranslUnitId);
    ed = Manager::Get()->GetEditorManager()->Open(filename);
    if (ed)
        ed->GotoTokenPosition(line - 1, stc->GetTextRange(stc->WordStartPosition(pos, true), stc->WordEndPosition(pos, true)));
}

wxString ClangPlugin::GetCompilerInclDirs(const wxString& compId)
{
    std::map<wxString, wxString>::const_iterator idItr = m_compInclDirs.find(compId);
    if (idItr != m_compInclDirs.end())
        return idItr->second;

    Compiler* comp = CompilerFactory::GetCompiler(compId);
    wxFileName fn(wxEmptyString, comp->GetPrograms().CPP);
    wxString masterPath = comp->GetMasterPath();
    Manager::Get()->GetMacrosManager()->ReplaceMacros(masterPath);
    fn.SetPath(masterPath);
    if (!fn.FileExists())
        fn.AppendDir(wxT("bin"));
#ifdef __WXMSW__
    wxString command = fn.GetFullPath() + wxT(" -v -E -x c++ nul");
#else
    wxString command = fn.GetFullPath() + wxT(" -v -E -x c++ /dev/null");
#endif // __WXMSW__
    wxArrayString output, errors;
    wxExecute(command, output, errors, wxEXEC_NODISABLE);

    wxArrayString::const_iterator errItr = errors.begin();
    for (; errItr != errors.end(); ++errItr)
    {
        if (errItr->IsSameAs(wxT("#include <...> search starts here:")))
        {
            ++errItr;
            break;
        }
    }
    wxString includeDirs;
    for (; errItr != errors.end(); ++errItr)
    {
        if (errItr->IsSameAs(wxT("End of search list.")))
            break;
        includeDirs += wxT(" -I") + errItr->Strip(wxString::both);
    }
    return m_compInclDirs.insert(std::pair<wxString, wxString>(compId, includeDirs)).first->second;
}

wxString ClangPlugin::GetSourceOf(cbEditor* ed)
{
    cbProject* project = nullptr;
    ProjectFile* opf = ed->GetProjectFile();
    if (opf)
        project = opf->GetParentProject();
    if (!project)
        project = Manager::Get()->GetProjectManager()->GetActiveProject();

    wxFileName theFile(ed->GetFilename());
    wxFileName candidateFile;
    bool isCandidate;
    wxArrayString fileArray;
    wxDir::GetAllFiles(theFile.GetPath(wxPATH_GET_VOLUME), &fileArray, theFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
    wxFileName currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
    if (isCandidate)
        candidateFile = currentCandidateFile;
    else if (currentCandidateFile.IsOk())
        return currentCandidateFile.GetFullPath();

    fileArray.Clear();
    EditorManager* edMgr = Manager::Get()->GetEditorManager();
    for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
    {
        cbEditor* edit = edMgr->GetBuiltinEditor(i);
        if (!edit)
            continue;

        ProjectFile* pf = edit->GetProjectFile();
        if (!pf)
            continue;

        fileArray.Add(pf->file.GetFullPath());
    }
    currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
    if (!isCandidate && currentCandidateFile.IsOk())
        return currentCandidateFile.GetFullPath();

    if (project)
    {
        fileArray.Clear();
        for (FilesList::const_iterator it = project->GetFilesList().begin(); it != project->GetFilesList().end(); ++it)
        {
            ProjectFile* pf = *it;
            if (!pf)
                continue;

            fileArray.Add(pf->file.GetFullPath());
        }
        currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
        if (isCandidate && !candidateFile.IsOk())
            candidateFile = currentCandidateFile;
        else if (currentCandidateFile.IsOk())
            return currentCandidateFile.GetFullPath();

        wxArrayString dirs = project->GetIncludeDirs();
        for (int i = 0; i < project->GetBuildTargetsCount(); ++i)
        {
            ProjectBuildTarget* target = project->GetBuildTarget(i);
            if (target)
            {
                for (size_t ti = 0; ti < target->GetIncludeDirs().GetCount(); ++ti)
                {
                    wxString dir = target->GetIncludeDirs()[ti];
                    if (dirs.Index(dir) == wxNOT_FOUND)
                        dirs.Add(dir);
                }
            }
        }
        for (size_t i = 0; i < dirs.GetCount(); ++i)
        {
            wxString dir = dirs[i];
            Manager::Get()->GetMacrosManager()->ReplaceMacros(dir);
            wxFileName dname(dir);
            if (!dname.IsAbsolute())
                dname.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE, project->GetBasePath());
            fileArray.Clear();
            wxDir::GetAllFiles(dname.GetPath(), &fileArray, theFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
            currentCandidateFile = FindSourceIn(fileArray, theFile, isCandidate);
            if (isCandidate)
                candidateFile = currentCandidateFile;
            else if (currentCandidateFile.IsOk())
                return currentCandidateFile.GetFullPath();
        }
    }
    if (candidateFile.IsOk())
        return candidateFile.GetFullPath();
    return wxEmptyString;
}

wxFileName ClangPlugin::FindSourceIn(const wxArrayString& candidateFilesArray, const wxFileName& activeFile, bool& isCandidate)
{
    bool extStartsWithCapital = wxIsupper(activeFile.GetExt()[0]);
    wxFileName candidateFile;
    for (size_t i = 0; i < candidateFilesArray.GetCount(); ++i)
    {
        wxFileName currentCandidateFile(candidateFilesArray[i]);
        if (IsSourceOf(currentCandidateFile, activeFile, isCandidate))
        {
            bool isUpper = wxIsupper(currentCandidateFile.GetExt()[0]);
            if (isUpper == extStartsWithCapital && !isCandidate)
                return currentCandidateFile;
            else
                candidateFile = currentCandidateFile;
        }
    }
    isCandidate = true;
    return candidateFile;
}

bool ClangPlugin::IsSourceOf(const wxFileName& candidateFile, const wxFileName& activeFile, bool& isCandidate)
{
    if (candidateFile.GetName().CmpNoCase(activeFile.GetName()) == 0)
    {
        isCandidate = (candidateFile.GetName() != activeFile.GetName());
        if (FileTypeOf(candidateFile.GetFullName()) == ftSource)
        {
            if (candidateFile.GetPath() != activeFile.GetPath())
            {
                wxArrayString fileArray;
                wxDir::GetAllFiles(candidateFile.GetPath(wxPATH_GET_VOLUME), &fileArray, candidateFile.GetName() + wxT(".*"), wxDIR_FILES | wxDIR_HIDDEN);
                for (size_t i = 0; i < fileArray.GetCount(); ++i)
                    if (wxFileName(fileArray[i]).GetFullName() == activeFile.GetFullName())
                        return false;
            }
            return candidateFile.FileExists();
        }
    }
    return false;
}

void ClangPlugin::OnTimer(wxTimerEvent& event)
{
    if (!IsAttached())
        return;
    const int evId = event.GetId();
    if (evId == idEdOpenTimer)
    {
        cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
        if (!ed || !IsProviderFor(ed))
            return;
        else if (m_Proxy.GetTranslationUnitId(ed->GetFilename()) != wxNOT_FOUND)
        {
            m_DiagnosticTimer.Start(DIAGNOSTIC_DELAY, wxTIMER_ONE_SHOT);
            return;
        }
        wxString compileCommand;
        ProjectFile* pf = ed->GetProjectFile();
        ProjectBuildTarget* target = nullptr;
        Compiler* comp = nullptr;
        if (pf)
        {
            target = pf->GetParentProject()->GetBuildTarget(pf->GetBuildTargets()[0]);
            comp = CompilerFactory::GetCompiler(target->GetCompilerID());
            if (pf->GetUseCustomBuildCommand(target->GetCompilerID()))
            {
                compileCommand = pf->GetCustomBuildCommand(target->GetCompilerID()).AfterFirst(wxT(' '));
            }
        }
        if (compileCommand.IsEmpty())
            compileCommand = wxT("$options $includes");
        cbProject* proj = (pf ? pf->GetParentProject() : nullptr);
        if (!comp && proj)
            comp = CompilerFactory::GetCompiler(proj->GetCompilerID());
        if (!comp)
        {
            cbProject* tmpPrj = Manager::Get()->GetProjectManager()->GetActiveProject();
            if (tmpPrj)
                comp = CompilerFactory::GetCompiler(tmpPrj->GetCompilerID());
        }
        if (!comp)
            comp = CompilerFactory::GetDefaultCompiler();
        comp->GetCommandGenerator(proj)->GenerateCommandLine(compileCommand, target, pf, ed->GetFilename(),
                                                             g_InvalidStr, g_InvalidStr, g_InvalidStr );
        wxStringTokenizer tokenizer(compileCommand);
        compileCommand.Empty();
        wxString pathStr;
        while (tokenizer.HasMoreTokens())
        {
            wxString flag = tokenizer.GetNextToken();
            // make all include paths absolute, so clang does not choke if Code::Blocks switches directories
            if (flag.StartsWith(wxT("-I"), &pathStr))
            {
                wxFileName path(pathStr);
                if (path.Normalize(wxPATH_NORM_ALL & ~wxPATH_NORM_CASE))
                    flag = wxT("-I") + path.GetFullPath();
            }
            compileCommand += flag + wxT(" ");
        }
        compileCommand += GetCompilerInclDirs(comp->GetID());
        if (FileTypeOf(ed->GetFilename()) == ftHeader) // try to find the associated source
        {
            const wxString& source = GetSourceOf(ed);
            if (!source.IsEmpty())
            {
                m_Proxy.CreateTranslationUnit(source, compileCommand);
                if (m_Proxy.GetTranslationUnitId(ed->GetFilename()) != wxNOT_FOUND)
                    return; // got it
            }
        }
        m_Proxy.CreateTranslationUnit(ed->GetFilename(), compileCommand);
        m_DiagnosticTimer.Start(DIAGNOSTIC_DELAY, wxTIMER_ONE_SHOT);
    }
    else if (evId == idReparseTimer)
    {
        EditorManager* edMgr = Manager::Get()->GetEditorManager();
        cbEditor* ed = edMgr->GetBuiltinActiveEditor();
        if (!ed)
            return;
        if (ed != m_pLastEditor)
        {
            m_TranslUnitId = m_Proxy.GetTranslationUnitId(ed->GetFilename());
            m_pLastEditor = ed;
        }
        if (m_TranslUnitId == wxNOT_FOUND)
            return;
        std::map<wxString, wxString> unsavedFiles;
        for (int i = 0; i < edMgr->GetEditorsCount(); ++i)
        {
            ed = edMgr->GetBuiltinEditor(i);
            if (ed && ed->GetModified())
                unsavedFiles.insert(std::make_pair(ed->GetFilename(), ed->GetControl()->GetText()));
        }
        m_Proxy.Reparse(m_TranslUnitId, unsavedFiles);
        DiagnoseEd(m_pLastEditor, dlMinimal);
    }
    else if (evId == idDiagnosticTimer)
    {
        cbEditor* ed = Manager::Get()->GetEditorManager()->GetBuiltinActiveEditor();
        if (!ed)
            return;
        if (ed != m_pLastEditor)
        {
            m_TranslUnitId = m_Proxy.GetTranslationUnitId(ed->GetFilename());
            m_pLastEditor = ed;
        }
        if (m_TranslUnitId == wxNOT_FOUND)
            return;
        DiagnoseEd(ed, dlFull);
    }
    else
        event.Skip();
}

void ClangPlugin::OnEditorHook(cbEditor* ed, wxScintillaEvent& event)
{
    event.Skip();
    if (!IsProviderFor(ed))
        return;
    if (event.GetEventType() == wxEVT_SCI_MODIFIED)
    {
        if (event.GetModificationType() & (wxSCI_MOD_INSERTTEXT | wxSCI_MOD_DELETETEXT))
        {
            m_ReparseTimer.Start(REPARSE_DELAY, wxTIMER_ONE_SHOT);
            m_DiagnosticTimer.Start(DIAGNOSTIC_DELAY, wxTIMER_ONE_SHOT);
        }
    }
}

void ClangPlugin::DiagnoseEd(cbEditor* ed, DiagnosticLevel diagLv)
{
    std::vector<ClDiagnostic> diagnostics;
    m_Proxy.GetDiagnostics(m_TranslUnitId, diagnostics);
    cbStyledTextCtrl* stc = ed->GetControl();
    if (diagLv == dlFull)
        stc->AnnotationClearAll();
    const int warningIndicator = 0; // predefined
    const int errorIndicator = 15; // hopefully we do not clash with someone else...
    stc->SetIndicatorCurrent(warningIndicator);
    stc->IndicatorClearRange(0, stc->GetLength());
    stc->IndicatorSetStyle(errorIndicator, wxSCI_INDIC_SQUIGGLE);
    stc->IndicatorSetForeground(errorIndicator, *wxRED);
    stc->SetIndicatorCurrent(errorIndicator);
    stc->IndicatorClearRange(0, stc->GetLength());
    const wxString& fileNm = ed->GetFilename();
    for ( std::vector<ClDiagnostic>::const_iterator dgItr = diagnostics.begin();
          dgItr != diagnostics.end(); ++dgItr )
    {
        //Manager::Get()->GetLogManager()->Log(dgItr->file + wxT(" ") + dgItr->message + F(wxT(" %d, %d"), dgItr->range.first, dgItr->range.second));
        if (dgItr->file != fileNm)
            continue;
        if (diagLv == dlFull)
        {
            wxString str = stc->AnnotationGetText(dgItr->line - 1);
            if (!str.IsEmpty())
                str += wxT('\n');
            stc->AnnotationSetText(dgItr->line - 1, str + dgItr->message);
            stc->AnnotationSetStyle(dgItr->line - 1, 50);
        }
        int pos = stc->PositionFromLine(dgItr->line - 1) + dgItr->range.first - 1;
        int range = dgItr->range.second - dgItr->range.first;
        if (range == 0)
        {
            range = stc->WordEndPosition(pos, true) - pos;
            if (range == 0)
            {
                pos = stc->WordStartPosition(pos, true);
                range = stc->WordEndPosition(pos, true) - pos;
            }
        }
        if (dgItr->severity == sError)
            stc->SetIndicatorCurrent(errorIndicator);
        else if (dgItr != diagnostics.begin() && dgItr->line == (dgItr - 1)->line && dgItr->range.first <= (dgItr - 1)->range.second)
            continue; // do not overwrite the last indicator
        else
            stc->SetIndicatorCurrent(warningIndicator);
        stc->IndicatorFillRange(pos, range);
    }
    if (diagLv == dlFull)
        stc->AnnotationSetVisible(wxSCI_ANNOTATION_BOXED);
}
