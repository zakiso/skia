/*
 * Copyright 2017 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bookmaker.h"

#include "SkOSFile.h"
#include "SkOSPath.h"

#define FPRINTF(...)                \
    if (fDebugOut) {                \
        SkDebugf(__VA_ARGS__);      \
    }                               \
    fprintf(fOut, __VA_ARGS__)

const char* SubtopicKeys::kGeneratedSubtopics[] = {
    kConstants, kDefines, kTypedefs, kMembers, kClasses, kStructs, kConstructors,
    kOperators, kMemberFunctions, kRelatedFunctions
};

const char* kConstTableStyle =
"<style>"                                                                                      "\n"
    ".td_const td, th { border: 2px solid #dddddd; text-align: left; padding: 8px; }"          "\n"
    ".tr_const tr:nth-child(even) { background-color: #f0f0f0; }"                              "\n"
    ".td2_const td:first-child + td { text-align: center; }"                                   "\n"
"</style>"                                                                                     "\n";

const char* kTableDeclaration = "<table style='border-collapse: collapse; width: 62.5em'>";

#define kTD_Base         "border: 2px solid #dddddd; padding: 8px; "
#define kTH_Left         "<th style='text-align: left; "   kTD_Base "'>"
#define kTH_Center       "<th style='text-align: center; " kTD_Base "'>"

string kTD_Left    = "    <td style='text-align: left; "   kTD_Base "'>";
string kTD_Center  = "    <td style='text-align: center; " kTD_Base "'>";
string kTR_Dark    =   "  <tr style='background-color: #f0f0f0; '>";

const char* kAllConstTableHeader =  "  <tr>" kTH_Left   "Const</th>"                            "\n"
                                             kTH_Center "Value</th>"                            "\n"
                                             kTH_Left   "Description</th>" "</tr>";
const char* kSubConstTableHeader =  "  <tr>" kTH_Left   "Const</th>"                            "\n"
                                             kTH_Center "Value</th>"                            "\n"
                                             kTH_Left   "Details</th>"                          "\n"
                                             kTH_Left   "Description</th>" "</tr>";
const char* kAllMemberTableHeader = "  <tr>" kTH_Left   "Type</th>"                             "\n"
                                             kTH_Left   "Member</th>"                           "\n"
                                             kTH_Left   "Description</th>" "</tr>";
const char* kSubMemberTableHeader = "  <tr>" kTH_Left   "Type</th>"                             "\n"
                                             kTH_Left   "Member</th>"                           "\n"
                                             kTH_Left   "Details</th>"                          "\n"
                                             kTH_Left   "Description</th>" "</tr>";
const char* kTopicsTableHeader    = "  <tr>" kTH_Left   "Topic</th>"                            "\n"
                                             kTH_Left   "Description</th>" "</tr>";

static string html_file_name(string bmhFileName) {
    SkASSERT("docs" == bmhFileName.substr(0, 4));
    SkASSERT('\\' == bmhFileName[4] || '/' == bmhFileName[4]);
    SkASSERT(".bmh" == bmhFileName.substr(bmhFileName.length() - 4));
    string result = bmhFileName.substr(5, bmhFileName.length() - 4 - 5);
    return result;
}

string MdOut::anchorDef(string str, string name) {
    if (fValidate) {
        string htmlName = html_file_name(fFileName);
        vector<AnchorDef>& allDefs = fAllAnchorDefs[htmlName];
        if (!std::any_of(allDefs.begin(), allDefs.end(),
                [str](AnchorDef compare) { return compare.fDef == str; } )) {
            MarkType markType = fLastDef->fMarkType;
            if (MarkType::kMethod == markType
                    && std::any_of(fLastDef->fChildren.begin(), fLastDef->fChildren.end(),
                    [](const Definition* compare) {
                    return IncompleteAllowed(compare->fMarkType); } )) {
                markType = MarkType::kDeprecated;
            }
            if (MarkType::kMethod == markType && fLastDef->fClone) {
                markType = MarkType::kDeprecated;  // TODO: hack to allow missing reference
            }
            allDefs.push_back( { str, markType } );
        }
    }
    return "<a name='" + str + "'>" + name + "</a>";
}

string MdOut::anchorRef(string ref, string name) {
    if (fValidate) {
        string htmlName;
        size_t hashIndex = ref.find('#');
        if (string::npos != hashIndex && "https://" != ref.substr(0, 8)) {
            if (0 == hashIndex) {
                htmlName = html_file_name(fFileName);
            } else {
                htmlName = ref.substr(0, hashIndex);
            }
            vector<string>& allRefs = fAllAnchorRefs[htmlName];
            string refPart = ref.substr(hashIndex + 1);
            if (allRefs.end() == std::find(allRefs.begin(), allRefs.end(), refPart)) {
                allRefs.push_back(refPart);
            }
        }
    }
    SkASSERT(string::npos != ref.find('#') || string::npos != ref.find("https://"));
    return "<a href='" + ref + "'>" + name + "</a>";
}

string MdOut::anchorLocalRef(string ref, string name) {
    return this->anchorRef("#" + ref, name);
}

string MdOut::tableDataCodeRef(string ref, string name) {
    return kTD_Left + this->anchorRef(ref, "<code>" + name + "</code>") + "</td>";
}

string MdOut::tableDataCodeLocalRef(string ref, string name) {
    return this->tableDataCodeRef("#" + ref, name);
}

string MdOut::tableDataCodeLocalRef(string name) {
    return this->tableDataCodeLocalRef(name, name);
}

string MdOut::tableDataCodeRef(const Definition* ref) {
    return this->tableDataCodeLocalRef(ref->fFiddle, ref->fName);
}

string MdOut::tableDataCodeDef(string def, string name) {
    return kTD_Left + this->anchorDef(def, "<code>" + name + "</code>") + "</td>";
}

string MdOut::tableDataCodeDef(const Definition* def) {
    return this->tableDataCodeDef(def->fFiddle, def->fName);
}

static string table_data_const(const Definition* def, const char** textStartPtr) {
    TextParser parser(def);
    SkAssertResult(parser.skipToEndBracket('\n'));
    string constant = string(def->fContentStart, (int) (parser.fChar - def->fContentStart));
    if (textStartPtr) {
        *textStartPtr = parser.fChar;
    }
    return kTD_Center + constant + "</td>";
}

static string out_table_data_description_start() {
    return kTD_Left;
}

static string out_table_data_description(string str) {
    return kTD_Left + str + "</td>";
}

static string out_table_data_description(const Definition* def) {
    return out_table_data_description(string(def->fContentStart,
            (int) (def->fContentEnd - def->fContentStart)));
}

static string out_table_data_details(string details) {
    return kTD_Left + details + "</td>";
}

#undef kConstTDBase
#undef kTH_Center

static void add_ref(string leadingSpaces, string ref, string* result) {
    *result += leadingSpaces + ref;
}

static string preformat(string orig) {
    string result;
    for (auto c : orig) {
        if ('<' == c) {
          result += "&lt;";
        } else if ('>' == c) {
          result += "&gt;";
        } else {
            result += c;
        }
    }
    return result;
}

static bool all_lower(string ref) {
	for (auto ch : ref) {
		if (!islower(ch)) {
			return false;
		}
	}
	return true;
}

// from https://stackoverflow.com/questions/3418231/replace-part-of-a-string-with-another-string
void replace_all(string& str, const string& from, const string& to) {
    SkASSERT(!from.empty());
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

// detail strings are preceded by an example comment to check readability
void MdOut::addPopulators() {
    auto populator = [this](string key, string singular, string plural, string oneLiner,
            string details) -> void {
        fPopulators[key].fSingular = singular;
        fPopulators[key].fPlural = plural;
        fPopulators[key].fOneLiner = oneLiner;
        fPopulators[key].fDetails = details;
    };
    populator(SubtopicKeys::kClasses, "Class", "Class Declarations",
            "embedded class members",
            /* SkImageInfo */ "uses <code>class</code> to declare the public data structures"
                              " and interfaces.");
    populator(SubtopicKeys::kConstants, "Constant", "Constants",
            "enum and enum class, and their const values",
            /* SkImageInfo */ "defines related constants are using <code>enum</code>,"
                              " <code>enum class</code>,  <code>#define</code>,"
                              " <code>const</code>, and <code>constexpr</code>.");
    populator(SubtopicKeys::kConstructors, "Constructor", "Constructors",
            "functions that construct",
            /* SkImageInfo */ "can be constructed or initialized by these functions,"
                              " including <code>class</code> constructors.");
    populator(SubtopicKeys::kDefines, "Define", "Defines",
            "preprocessor definitions of functions, values",
            /* SkImageInfo */ "uses preprocessor definitions to inline code and constants,"
                              " and to abstract platform-specific functionality.");
    populator(SubtopicKeys::kMemberFunctions, "Member Function", "Member Functions",
            "static and local functions",
            /* SkImageInfo */ "uses member functions to read and modify structure properties.");
    populator(SubtopicKeys::kMembers, "Member", "Members",
            "member values",
            /* SkImageInfo */ "contains members that may be read and written directly without using"
                              " a member function.");
    populator(SubtopicKeys::kOperators, "Operator", "Operators",
            "operator overloading functions",
            /* SkImageInfo */ "defines member functions with arithmetic equivalents.");
    populator(SubtopicKeys::kRelatedFunctions, "Related Function", "Related Functions",
            "similar functions grouped together",
            /* SkImageInfo */ "defines related functions that share a topic.");
    populator(SubtopicKeys::kStructs, "Struct", "Struct Declarations",
            "embedded struct members",
            /* SkImageInfo */ "uses <code>struct</code> to declare the public data"
                              " structures and interfaces.");
    populator(SubtopicKeys::kTypedefs, "Typedef", "Typedef Declarations",
            "types defined in terms of other types",
            /* SkImageInfo */ "uses <code>typedef</code> to define a data type.");
}

Definition* MdOut::checkParentsForMatch(Definition* test, string ref) const {
    bool isSubtopic = MarkType::kSubtopic == test->fMarkType
            || MarkType::kTopic == test->fMarkType;
    do {
        if (!test->isRoot()) {
            continue;
        }
        bool localTopic = MarkType::kSubtopic == test->fMarkType
                || MarkType::kTopic == test->fMarkType;
        if (localTopic != isSubtopic) {
            continue;
        }
        string prefix(isSubtopic ? "_" : "::");
        RootDefinition* root = test->asRoot();
        string prefixed = root->fName + prefix + ref;
        if (Definition* def = root->find(prefixed, RootDefinition::AllowParens::kYes)) {
            return def;
        }
    } while ((test = test->fParent));
    return nullptr;
}

static bool formula_or_code(BmhParser::Resolvable resolvable) {
    return  BmhParser::Resolvable::kFormula == resolvable
            || BmhParser::Resolvable::kCode == resolvable;
}

static void fixup_const_function_name(string* ref) {
    const char spaceConst[] = " const";
    size_t spacePos = ref->rfind(spaceConst);
    if (string::npos != spacePos && spacePos == ref->length() - sizeof(spaceConst) + 1) {
        *ref = ref->substr(0, spacePos) + "_const";
    }
}

struct BraceState {
    BraceState(RootDefinition* root, string name, const char* ch, KeyWord last, KeyWord keyWord,
            int count)
        : fRoot(root)
        , fName(name)
        , fChar(ch)
        , fLastKey(last)
        , fKeyWord(keyWord)
        , fBraceCount(count) {
    }

    RootDefinition* fRoot;
    string fName;
    const char* fChar;
    KeyWord fLastKey;
    KeyWord fKeyWord;
    int fBraceCount;
};

// FIXME: preserve inter-line spaces and don't add new ones
string MdOut::addReferences(const char* refStart, const char* refEnd,
        BmhParser::Resolvable resolvable) {
    string result;
    MethodParser t(fRoot ? fRoot->fName : string(), fFileName, refStart, refEnd, fLineCount);
    bool lineStart = true;
//    bool structClassSet = false;
    string ref;
    string leadingSpaces;
    int distFromParam = 99;
    const char* lastLine = nullptr;
    vector<BraceState> braceStack;
    KeyWord lastKeyWord = KeyWord::kNone;
    KeyWord keyWord = KeyWord::kNone;
    if (BmhParser::Resolvable::kCode == resolvable) {
        braceStack.push_back({fRoot, fRoot->fName, t.fChar, lastKeyWord, keyWord, 0});
    }
    do {
        ++distFromParam;
        const char* base = t.fChar;
        t.skipWhiteSpace();
        const char* wordStart = t.fChar;
        if (formula_or_code(resolvable) && !t.eof() && '"' == t.peek()) {
            t.next();
            t.skipToEndBracket('"');
            t.next();
            continue;
        }
        if (BmhParser::Resolvable::kCode == resolvable) {
            int priorBrace = braceStack.back().fBraceCount;
            int braceCount = priorBrace + t.skipToMethodStart();
            if (braceCount > priorBrace) {
                string name;
                if (KeyWord::kClass == keyWord || KeyWord::kStruct == keyWord) {
                    name = ref;
                } else if (KeyWord::kClass == lastKeyWord && KeyWord::kPublic == keyWord) {
                    SkASSERT(lastLine);
                    TextParser parser(t.fFileName, lastLine, t.fChar, t.fLineCount);
                    parser.skipSpace();
                    SkAssertResult(parser.skipExact("class "));
                    parser.skipSpace();
                    const char* nameStart = parser.fChar;
                    parser.skipToSpace();
                    name = string(nameStart, parser.fChar - nameStart);
                }
                if ("" != name) {
                    const Definition* classDef = this->isDefined(t, name, resolvable);
                    if (!classDef) {
                        string className = fRoot->csParent()->fName;
                        string withRoot = className + "::" + name;
                        classDef = this->isDefined(t, withRoot, resolvable);
                        SkASSERT(classDef);
                    }
                    if (classDef->isRoot()) {
                        fRoot = const_cast<Definition*>(classDef)->asRoot();
                        t.setLocalName(name);
                    }
                }
                braceStack.push_back({fRoot, name, t.fChar, lastKeyWord, keyWord, braceCount});
            } else if (braceCount < priorBrace) {
                lastKeyWord = braceStack.back().fLastKey;
                keyWord = braceStack.back().fKeyWord;
                braceStack.pop_back();
                if (KeyWord::kClass == keyWord || KeyWord::kStruct == keyWord) {
                    fRoot = braceStack.back().fRoot;
                    t.setLocalName(braceStack.back().fName);
                }
            }
        } else {
            (void) t.skipToMethodStart();
        }
        const char* start = t.fChar;
        if (fParamEnd <= start) {
            fParamEnd = nullptr;
        }
        if (wordStart < start) {
            if (lineStart) {
                lineStart = false;
            } else {
                wordStart = base;
            }
            string nonWord = string(wordStart, start - wordStart);
            if (BmhParser::Resolvable::kFormula == resolvable) {
                string unbreakable;
                bool comma = false;
                for (char c : nonWord) {
                    if (string::npos != string("\\`*_{}[]()#+-.!").find(c)) {
                        unbreakable += '\\';
                    }
                    if (' ' == c && !comma) {
                        unbreakable += "&nbsp;";
                    } else {
                        unbreakable += c;
                    }
                    comma = ',' == c;
                }
                nonWord = unbreakable;
            }
            result += nonWord;
            if ('\n' != result.back()) {
                while (start > wordStart && '\n' == start[-1]) {
                    result += '\n';
                    --start;
                }
            }
        }
        if (lineStart) {
            lineStart = false;
        } else {
            leadingSpaces = string(base, wordStart - base);
        }
        t.skipToMethodEnd(resolvable);
        if (base == t.fChar) {
            if (!t.eof() && '~' == base[0] && !isalnum(base[1])) {
                t.next();
            } else {
                break;
            }
        }
        if (start >= t.fChar) {
            continue;
        }
        if (!t.eof() && '"' == t.peek() && start > wordStart && '"' == start[-1]) {
            continue;
        }
        ref = string(start, t.fChar - start);
        if (fParamEnd
                && islower(start[0]) && ('k' != start[0] || !isupper(start[1]))) {
            if (' ' == start[-1] && ' ' != result.back()) {
                result += ' ';
            }
            result += ref;
            continue;
        }
        if (BmhParser::Resolvable::kCode == resolvable) {
            fixup_const_function_name(&ref);
        }
        if (Definition* def = this->isDefined(t, ref, resolvable)) {
            if (MarkType::kExternal == def->fMarkType) {
                (void) this->anchorRef("undocumented#" + ref, "");   // for anchor validate
                add_ref(leadingSpaces, ref, &result);
                continue;
            }
            SkASSERT(def->fFiddle.length());
            if (BmhParser::Resolvable::kSimple != resolvable
                    && !t.eof() && '(' == t.peek() && t.strnchr(')', t.fEnd)) {
                TextParserSave tSave(&t);
                if (!t.skipToBalancedEndBracket('(', ')')) {
                    tSave.restore();
                    t.reportError("missing close paren");
                    fAddRefFailed = true;
                    return result;
                }
//                t.next();
                SkASSERT(')' == t.fChar[-1]);
                fParamEnd = t.fChar - 1;    // don't resolve lower case words til param end
                string fullRef = string(start, t.fChar - start);
                // if _2 etc alternates are defined, look for paren match
                // may ignore () if ref is all lower case and resolvable is not code
                // otherwise flag as error
                int suffix = '2';
                bool foundMatch = false;
                Definition* altDef = def;
                while (altDef && suffix <= '9') {
                    if ((foundMatch = altDef->paramsMatch(fullRef, ref))) {
                        def = altDef;
                        ref = fullRef;
                        break;
                    }
                    string altTest = ref + '_';
                    altTest += suffix++;
                    altDef = this->isDefined(t, altTest, BmhParser::Resolvable::kOut);
                }
                if (suffix > '9') {
                    t.reportError("too many alts");
                    fAddRefFailed = true;
                    return result;
                }
                if (!foundMatch) {
                    if (!(def = this->isDefined(t, fullRef, resolvable))) {
                        if (formula_or_code(resolvable)) {
                            // TODO: look for looser mapping -- if methods name match, look for
                            //   unique mapping based on number of parameters
                            // for now, just look for function name match
                            def = this->isDefined(t, ref, resolvable);
                        }
                        if (!def && !result.size()) {
                            t.reportError("missing method");
                            fAddRefFailed = true;
                            return result;
                        }
                    }
                    ref = fullRef;
                }
                ref = ref.substr(0, ref.find('('));
                tSave.restore();
			} else if (BmhParser::Resolvable::kClone != resolvable &&
					all_lower(ref) && (t.eof() || '(' != t.peek())) {
				add_ref(leadingSpaces, ref, &result);
				continue;
			}
            if (!def) {
                t.reportError("missing method");
                fAddRefFailed = true;
                return result;
            }
			result += linkRef(leadingSpaces, def, ref, resolvable);
            if (!t.eof() && '(' == t.peek()) {
                result += t.next();  // skip open paren
            }
            continue;
        }
        if (!t.eof() && '(' == t.peek()) {
            if (!t.skipToEndBracket(')')) {
                t.reportError("missing close paren");
                fAddRefFailed = true;
                return result;
            }
            t.next();
            ref = string(start, t.fChar - start);
            if (Definition* def = this->isDefined(t, ref, BmhParser::Resolvable::kYes)) {
                SkASSERT(def->fFiddle.length());
				result += linkRef(leadingSpaces, def, ref, resolvable);
                continue;
            }
        }
// class, struct, and enum start with capitals
// methods may start with upper (static) or lower (most)

        // see if this should have been a findable reference

            // look for Sk / sk / SK ..
        if (!ref.compare(0, 2, "Sk") && ref != "Skew" && ref != "Skews" && ref != "Skewing"
              && ref != "Skip" && ref != "Skips") {
            if (BmhParser::Resolvable::kOut != resolvable && !formula_or_code(resolvable)) {
                t.reportError("missed Sk prefixed");
                fAddRefFailed = true;
                return result;
            }
        }
        if (!ref.compare(0, 2, "SK")) {
            if (BmhParser::Resolvable::kOut != resolvable && !formula_or_code(resolvable)) {
                t.reportError("missed SK prefixed");
                fAddRefFailed = true;
                return result;
            }
        }
        if (!isupper(start[0])) {
            // TODO:
            // look for all lowercase w/o trailing parens as mistaken method matches
            // will also need to see if Example Description matches var in example
            Definition* def = nullptr;
            if (fMethod && (def = fMethod->hasParam(ref))) {
				result += linkRef(leadingSpaces, def, ref, resolvable);
                fLastParam = def;
                distFromParam = 0;
                continue;
            } else if (!fInDescription && ref[0] != '0'
                    && string::npos != ref.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ")) {
                // FIXME: see isDefined(); check to see if fXX is a member of xx.fXX
                if (('f' != ref[0] && string::npos == ref.find("()"))
//                        || '.' != t.backup(ref.c_str())
                        && ('k' != ref[0] && string::npos == ref.find("_Private"))) {
                    if ('.' == wordStart[0] && (distFromParam >= 1 && distFromParam <= 16)) {
                        Definition* paramType = this->findParamType();
                        if (paramType) {
                            string fullName = paramType->fName + "::" + ref;
                            if (paramType->hasMatch(fullName)) {
								result += linkRef(leadingSpaces, paramType, ref, resolvable);
                                continue;
                            }
                        }
                    }
                    if (BmhParser::Resolvable::kSimple != resolvable
                            && BmhParser::Resolvable::kOut != resolvable
                            && !formula_or_code(resolvable)) {
                        t.reportError("missed camelCase");
                        fAddRefFailed = true;
                        return result;
                    }
                }
            }
            KeyWord newKeyWord = IncludeParser::FindKey(start, start + ref.length());
            lastLine = nullptr;
            if (KeyWord::kPrivate != newKeyWord && KeyWord::kProtected != newKeyWord
                    && KeyWord::kPublic != newKeyWord) {
                lastKeyWord = keyWord;
                keyWord = newKeyWord;
            } else if (BmhParser::Resolvable::kCode == resolvable && ':' != t.peek()) {
                lastLine = t.fLine;
                lastKeyWord = keyWord;
                keyWord = newKeyWord;
            }
            add_ref(leadingSpaces, ref, &result);
            continue;
        }
        auto topicIter = fBmhParser.fTopicMap.find(ref);
        if (topicIter != fBmhParser.fTopicMap.end()) {
			result += linkRef(leadingSpaces, topicIter->second, ref, resolvable);
            continue;
        }
        bool startsSentence = t.sentenceEnd(start);
        if (!t.eof() && ' ' != t.peek()) {
			add_ref(leadingSpaces, ref, &result);
            continue;
        }
        if (t.fChar + 1 >= t.fEnd || (!isupper(t.fChar[1]) && startsSentence)) {
			add_ref(leadingSpaces, ref, &result);
            continue;
        }
        if (isupper(t.fChar[1]) && startsSentence) {
            TextParser next(t.fFileName, &t.fChar[1], t.fEnd, t.fLineCount);
            string nextWord(next.fChar, next.wordEnd() - next.fChar);
            if (this->isDefined(t, nextWord, BmhParser::Resolvable::kYes)) {
				add_ref(leadingSpaces, ref, &result);
                continue;
            }
        }
        Definition* def = this->checkParentsForMatch(fSubtopic, ref);
        if (!def) {
            def = this->checkParentsForMatch(fRoot, ref);
        }
        if (def) {
 			result += this->linkRef(leadingSpaces, def, ref, resolvable);
            continue;
        }
        if (BmhParser::Resolvable::kOut != resolvable &&
                !formula_or_code(resolvable)) {
            t.reportError("undefined reference");
            fAddRefFailed = true;
        } else {
			add_ref(leadingSpaces, ref, &result);
        }
    } while (!t.eof());
    return result;
}

bool MdOut::buildReferences(const char* docDir, const char* mdFileOrPath) {
    if (!sk_isdir(mdFileOrPath)) {
        SkDebugf("must pass directory %s\n", mdFileOrPath);
        SkDebugf("pass -i SkXXX.h to build references for a single include\n");
        return false;
    }
    fInProgress = true;
    SkOSFile::Iter it(docDir, ".bmh");
    for (SkString file; it.next(&file); ) {
        if (!fIncludeParser.references(file)) {
            continue;
        }
        SkString p = SkOSPath::Join(docDir, file.c_str());
        if (!this->buildRefFromFile(p.c_str(), mdFileOrPath)) {
            SkDebugf("failed to parse %s\n", p.c_str());
            return false;
        }
    }
    return true;
}

bool MdOut::buildStatus(const char* statusFile, const char* outDir) {
    StatusIter iter(statusFile, ".bmh", StatusFilter::kInProgress);
    StatusFilter filter;
    for (string file; iter.next(&file, &filter); ) {
        SkString p = SkOSPath::Join(iter.baseDir().c_str(), file.c_str());
        const char* hunk = p.c_str();
        fInProgress = StatusFilter::kInProgress == filter;
        if (!this->buildRefFromFile(hunk, outDir)) {
            SkDebugf("failed to parse %s\n", hunk);
            return false;
        }
    }
    return true;
}

bool MdOut::buildRefFromFile(const char* name, const char* outDir) {
    if (!SkStrEndsWith(name, ".bmh")) {
        return true;
    }
    if (SkStrEndsWith(name, "markup.bmh")) {  // don't look inside this for now
        return true;
    }
    if (SkStrEndsWith(name, "illustrations.bmh")) {  // don't look inside this for now
        return true;
    }
    fFileName = string(name);
    string filename(name);
    if (filename.substr(filename.length() - 4) == ".bmh") {
        filename = filename.substr(0, filename.length() - 4);
    }
    size_t start = filename.length();
    while (start > 0 && (isalnum(filename[start - 1]) || '_' == filename[start - 1])) {
        --start;
    }
    string match = filename.substr(start);
    string header = match;
    filename = match + ".md";
    match += ".bmh";
    fOut = nullptr;
    string fullName;

    vector<string> keys;
    keys.reserve(fBmhParser.fTopicMap.size());
    for (const auto& it : fBmhParser.fTopicMap) {
        keys.push_back(it.first);
    }
    std::sort(keys.begin(), keys.end());
    for (auto key : keys) {
        string s(key);
        auto topicDef = fBmhParser.fTopicMap.at(s);
        if (topicDef->fParent) {
            continue;
        }
        if (string::npos == topicDef->fFileName.rfind(match)) {
            continue;
        }
        if (!fOut) {
            fullName = outDir;
            if ('/' != fullName.back()) {
                fullName += '/';
            }
            fullName += filename;
            fOut = fopen(filename.c_str(), "wb");
            if (!fOut) {
                SkDebugf("could not open output file %s\n", fullName.c_str());
                return false;
            }
            if (false) {    // try inlining the style
                FPRINTF("%s", kConstTableStyle);
            }
            size_t underscorePos = header.find('_');
            if (string::npos != underscorePos) {
                header.replace(underscorePos, 1, " ");
            }
            SkASSERT(string::npos == header.find('_'));
            this->writeString(header);
            this->lfAlways(1);
            this->writeString("===");
            this->lfAlways(1);
        }
        const Definition* prior = nullptr;
        this->markTypeOut(topicDef, &prior);
    }
    if (fOut) {
        this->writePending();
        fclose(fOut);
        fflush(fOut);
        if (ParserCommon::WrittenFileDiffers(fullName, filename)) {
            ParserCommon::CopyToFile(fullName, filename);
            SkDebugf("wrote %s\n", fullName.c_str());
        } else {
            remove(filename.c_str());
        }
        fOut = nullptr;
    }
    return !fAddRefFailed;
}

static bool contains_referenced_child(const Definition* found, const vector<string>& refs) {
    for (auto child : found->fChildren) {
        if (refs.end() != std::find_if(refs.begin(), refs.end(),
                    [child](string def) { return child->fName == def; } )) {
            return true;
        }
        if (contains_referenced_child(child, refs)) {
            return true;
        }
    }
    return false;
}

void MdOut::checkAnchors() {
    int missing = 0;
    for (auto bmhFile : fAllAnchorRefs) {
        auto defIter = fAllAnchorDefs.find(bmhFile.first);
        SkASSERT(fAllAnchorDefs.end() != defIter);
        vector<AnchorDef>& allDefs = defIter->second;
        std::sort(allDefs.begin(), allDefs.end(),
                [](const AnchorDef& a, const AnchorDef& b) { return a.fDef < b.fDef; } );
        std::sort(bmhFile.second.begin(), bmhFile.second.end());
        auto allDefsIter = allDefs.begin();
        auto allRefsIter = bmhFile.second.begin();
        for (;;) {
            bool allDefsEnded = allDefsIter == allDefs.end();
            bool allRefsEnded = allRefsIter == bmhFile.second.end();
            if (allDefsEnded && allRefsEnded) {
                break;
            }
            if (allRefsEnded || (!allDefsEnded && allDefsIter->fDef < *allRefsIter)) {
                if (MarkType::kParam != allDefsIter->fMarkType
                        && !IncompleteAllowed(allDefsIter->fMarkType)) {
                    // If undocumented but parent or child is referred to: good enough for now
                    bool goodEnough = false;
                    if ("undocumented" == defIter->first) {
                        auto iter = fBmhParser.fTopicMap.find(allDefsIter->fDef);
                        if (fBmhParser.fTopicMap.end() != iter) {
                            const Definition* found = iter->second;
                            if (string::npos != found->fFileName.find("undocumented")) {
                                const Definition* parent = found;
                                while ((parent = parent->fParent)) {
                                    if (bmhFile.second.end() != std::find_if(bmhFile.second.begin(),
                                            bmhFile.second.end(),
                                            [parent](string def) {
                                            return parent->fName == def; } )) {
                                        goodEnough = true;
                                        break;
                                    }
                                }
                                if (!goodEnough) {
                                    goodEnough = contains_referenced_child(found, bmhFile.second);
                                }
                            }
                        }
                    }
                    if (!goodEnough) {
                        SkDebugf("missing ref %s %s\n", defIter->first.c_str(),
                                allDefsIter->fDef.c_str());
                        missing++;
                    }
                }
                allDefsIter++;
            } else if (allDefsEnded || (!allRefsEnded && allDefsIter->fDef > *allRefsIter)) {
                if (fBmhParser.fExternals.end() == std::find_if(fBmhParser.fExternals.begin(),
                        fBmhParser.fExternals.end(), [allRefsIter](const RootDefinition& root) {
                        return *allRefsIter != root.fName; } )) {
                    SkDebugf("missing def %s %s\n", bmhFile.first.c_str(), allRefsIter->c_str());
                    missing++;
                }
                allRefsIter++;
            } else {
                SkASSERT(!allDefsEnded);
                SkASSERT(!allRefsEnded);
                SkASSERT(allDefsIter->fDef == *allRefsIter);
                allDefsIter++;
                allRefsIter++;
            }
            if (missing >= 10) {
                missing = 0;
            }
        }
    }
}

bool MdOut::checkParamReturnBody(const Definition* def) {
    TextParser paramBody(def);
    const char* descriptionStart = paramBody.fChar;
    if (!islower(descriptionStart[0]) && !isdigit(descriptionStart[0])) {
        paramBody.skipToNonName();
        string ref = string(descriptionStart, paramBody.fChar - descriptionStart);
        if (!this->isDefined(paramBody, ref, BmhParser::Resolvable::kYes)) {
            string errorStr = MarkType::kReturn == def->fMarkType ? "return" : "param";
            errorStr += " description must start with lower case";
            paramBody.reportError(errorStr.c_str());
            fAddRefFailed = true;
            return false;
        }
    }
    if ('.' == paramBody.fEnd[-1]) {
        paramBody.reportError("make param description a phrase; should not end with period");
        fAddRefFailed = true;
        return false;
    }
    return true;
}

void MdOut::childrenOut(Definition* def, const char* start) {
    if (MarkType::kDeprecated == def->fMarkType || MarkType::kExperimental == def->fMarkType) {
        return;
    }
    const char* end;
    fLineCount = def->fLineCount;
    if (MarkType::kEnumClass == def->fMarkType) {
        fEnumClass = def;
    }
    BmhParser::Resolvable resolvable = this->resolvable(def);
    const Definition* prior = nullptr;
    for (auto& child : def->fChildren) {
        if (MarkType::kPhraseParam == child->fMarkType) {
            continue;
        }
        end = child->fStart;
        if (BmhParser::Resolvable::kNo != resolvable) {
            this->resolveOut(start, end, resolvable);
        }
        this->markTypeOut(child, &prior);
        start = child->fTerminator;
    }
    if (BmhParser::Resolvable::kNo != resolvable) {
        end = def->fContentEnd;
        if (MarkType::kFormula == def->fMarkType && ' ' == start[0]) {
            this->writeSpace();
        }
        this->resolveOut(start, end, resolvable);
    }
    if (MarkType::kEnumClass == def->fMarkType) {
        fEnumClass = nullptr;
    }
}

// output header for subtopic for all consts: name, value, short descriptions (#Line)
// output link to in context #Const with moderate description
void MdOut::summaryOut(const Definition* def, MarkType markType, string name) {
    this->writePending();
    SkASSERT(TableState::kNone == fTableState);
    this->mdHeaderOut(3);
    FPRINTF("%s", name.c_str());
    this->lfAlways(2);
    FPRINTF("%s", kTableDeclaration);  // <table> with style info
    this->lfAlways(1);
    FPRINTF("%s", MarkType::kConst == markType ? kAllConstTableHeader : kAllMemberTableHeader);
    this->lfAlways(1);
    bool odd = true;
    for (auto child : def->fChildren) {
        if (markType != child->fMarkType) {
            continue;
        }
        auto oneLiner = std::find_if(child->fChildren.begin(), child->fChildren.end(),
                [](const Definition* test){ return MarkType::kLine == test->fMarkType; } );
        if (child->fChildren.end() == oneLiner) {
            child->reportError<void>("missing #Line");
            continue;
        }
        FPRINTF("%s", odd ? kTR_Dark.c_str() : "  <tr>");
        this->lfAlways(1);
        if (MarkType::kConst == markType) {
            FPRINTF("%s", tableDataCodeRef(child).c_str());
            this->lfAlways(1);
            FPRINTF("%s", table_data_const(child, nullptr).c_str());
        } else {
            string memberType;
            string memberName = this->getMemberTypeName(child, &memberType);
            SkASSERT(MarkType::kMember == markType);
            FPRINTF("%s", out_table_data_description(memberType).c_str());
            this->lfAlways(1);
            FPRINTF("%s", tableDataCodeLocalRef(memberName).c_str());
        }
        this->lfAlways(1);
        FPRINTF("%s", out_table_data_description(*oneLiner).c_str());
        this->lfAlways(1);
        FPRINTF("%s", "  </tr>");
        this->lfAlways(1);
        odd = !odd;
    }
    FPRINTF("</table>");
    this->lfAlways(1);
}

Definition* MdOut::csParent() {
    if (!fRoot) {
        return nullptr;
    }
    Definition* csParent = fRoot->csParent();
    if (!csParent) {
        const Definition* topic = fRoot;
        while (topic && MarkType::kTopic != topic->fMarkType) {
            topic = topic->fParent;
        }
        for (auto child : topic->fChildren) {
            if (child->isStructOrClass() || MarkType::kTypedef == child->fMarkType) {
                csParent = child;
                break;
            }
        }
        SkASSERT(csParent || string::npos == fRoot->fFileName.find("Sk")
                || string::npos != fRoot->fFileName.find("SkBlendMode_Reference.bmh"));
    }
    return csParent;
}

Definition* MdOut::findParamType() {
    SkASSERT(fMethod);
    TextParser parser(fMethod->fFileName, fMethod->fStart, fMethod->fContentStart,
            fMethod->fLineCount);
    string lastFull;
    do {
        parser.skipToAlpha();
        if (parser.eof()) {
            return nullptr;
        }
        const char* word = parser.fChar;
        parser.skipFullName();
        SkASSERT(!parser.eof());
        string name = string(word, parser.fChar - word);
        if (fLastParam->fName == name) {
            Definition* paramType = this->isDefined(parser, lastFull,
                    BmhParser::Resolvable::kOut);
            return paramType;
        }
        if (isupper(name[0])) {
            lastFull = name;
        }
    } while (true);
    return nullptr;
}

string MdOut::getMemberTypeName(const Definition* def, string* memberType) {
    TextParser parser(def->fFileName, def->fStart, def->fContentStart,
            def->fLineCount);
    parser.skipExact("#Member");
    parser.skipWhiteSpace();
    const char* typeStart = parser.fChar;
    const char* typeEnd = nullptr;
    const char* nameStart = nullptr;
    const char* nameEnd = nullptr;
    do {
        parser.skipToWhiteSpace();
        if (nameStart) {
            nameEnd = parser.fChar;
        }
        if (parser.eof()) {
            break;
        }
        const char* spaceLoc = parser.fChar;
        if (parser.skipWhiteSpace()) {
            typeEnd = spaceLoc;
            nameStart = parser.fChar;
        }
    } while (!parser.eof());
    SkASSERT(typeEnd);
    *memberType = string(typeStart, (int) (typeEnd - typeStart));
    replace_all(*memberType, " ", "&nbsp;");
    SkASSERT(nameStart);
    SkASSERT(nameEnd);
    return string(nameStart, (int) (nameEnd - nameStart));
}

bool MdOut::HasDetails(const Definition* def) {
    for (auto child : def->fChildren) {
        if (MarkType::kDetails == child->fMarkType) {
            return true;
        }
        if (MdOut::HasDetails(child)) {
            return true;
        }
    }
    return false;
}

void MdOut::htmlOut(string s) {
    SkASSERT(string::npos != s.find('<'));
    FPRINTF("%s", s.c_str());
}

Definition* MdOut::isDefinedByParent(RootDefinition* root, string ref) {
    if (ref == root->fName) {
        return root;
    }
    if (Definition* definition = root->find(ref, RootDefinition::AllowParens::kYes)) {
        return definition;
    }
    Definition* test = root;
    bool isSubtopic = MarkType::kSubtopic == root->fMarkType
        || MarkType::kTopic == root->fMarkType;
    do {
        if (!test->isRoot()) {
            continue;
        }
        bool testIsSubtopic = MarkType::kSubtopic == test->fMarkType
                || MarkType::kTopic == test->fMarkType;
        if (isSubtopic != testIsSubtopic) {
            continue;
        }
        RootDefinition* root = test->asRoot();
        for (auto& leaf : root->fBranches) {
            if (ref == leaf.first) {
                return leaf.second;
            }
            Definition* definition = leaf.second->find(ref,
                    RootDefinition::AllowParens::kYes);
            if (definition) {
                return definition;
            }
        }
        string prefix = isSubtopic ? "_" : "::";
        string prefixed = root->fName + prefix + ref;
        if (Definition* definition = root->find(prefixed, RootDefinition::AllowParens::kYes)) {
            return definition;
        }
        if (isSubtopic && isupper(prefixed[0])) {
            auto topicIter = fBmhParser.fTopicMap.find(prefixed);
            if (topicIter != fBmhParser.fTopicMap.end()) {
                return topicIter->second;
            }
        }
        if (isSubtopic) {
            string fiddlePrefixed = root->fFiddle + "_" + ref;
            auto topicIter = fBmhParser.fTopicMap.find(fiddlePrefixed);
            if (topicIter != fBmhParser.fTopicMap.end()) {
                return topicIter->second;
            }
        }
    } while ((test = test->fParent));
    return nullptr;
}

Definition* MdOut::isDefined(const TextParser& parser, string ref,
        BmhParser::Resolvable resolvable) {
    auto rootIter = fBmhParser.fClassMap.find(ref);
    if (rootIter != fBmhParser.fClassMap.end()) {
        return &rootIter->second;
    }
    auto typedefIter = fBmhParser.fTypedefMap.find(ref);
    if (typedefIter != fBmhParser.fTypedefMap.end()) {
        return &typedefIter->second;
    }
    auto enumIter = fBmhParser.fEnumMap.find(ref);
    if (enumIter != fBmhParser.fEnumMap.end()) {
        return &enumIter->second;
    }
    auto constIter = fBmhParser.fConstMap.find(ref);
    if (constIter != fBmhParser.fConstMap.end()) {
        return &constIter->second;
    }
    auto methodIter = fBmhParser.fMethodMap.find(ref);
    if (methodIter != fBmhParser.fMethodMap.end()) {
        return &methodIter->second;
    }
    auto aliasIter = fBmhParser.fAliasMap.find(ref);
    if (aliasIter != fBmhParser.fAliasMap.end()) {
        return aliasIter->second;
    }
    auto defineIter = fBmhParser.fDefineMap.find(ref);
    if (defineIter != fBmhParser.fDefineMap.end()) {
        return &defineIter->second;
    }
    for (auto& external : fBmhParser.fExternals) {
        if (external.fName == ref) {
            return &external;
        }
    }
    if (Definition* definition = this->isDefinedByParent(fRoot, ref)) {
        return definition;
    }
    if (Definition* definition = this->isDefinedByParent(fSubtopic, ref)) {
        return definition;
    }
    size_t doubleColon = ref.find("::");
    if (string::npos != doubleColon) {
        string className = ref.substr(0, doubleColon);
        auto classIter = fBmhParser.fClassMap.find(className);
        if (classIter != fBmhParser.fClassMap.end()) {
            RootDefinition& classDef = classIter->second;
            Definition* result = classDef.find(ref, RootDefinition::AllowParens::kYes);
            if (result) {
                return result;
            }
        }

    }
    if (!ref.compare(0, 2, "SK") || !ref.compare(0, 3, "sk_")
            || (('k' == ref[0] || 'g' == ref[0] || 'f' == ref[0]) &&
                ref.length() > 1 && isupper(ref[1]))) {
        // try with a prefix
        if ('k' == ref[0]) {
            for (auto& iter : fBmhParser.fEnumMap) {
                auto def = iter.second.find(ref, RootDefinition::AllowParens::kYes);
                if (def) {
                    return def;
                }
            }
            if (fEnumClass) {
                string fullName = fEnumClass->fName + "::" + ref;
                for (auto child : fEnumClass->fChildren) {
                    if (fullName == child->fName) {
                        return child;
                    }
                }
            }
            if (string::npos != ref.find("_Private")) {
                return nullptr;
            }
        }
        if ('f' == ref[0]) {
            // FIXME : find def associated with prior, e.g.: r.fX where 'SkPoint r' was earlier
                // need to have pushed last resolve on stack to do this
                // for now, just try to make sure that it's there and error if not
            if ('.' != parser.backup(ref.c_str())) {
                parser.reportError("fX member undefined");
                fAddRefFailed = true;
                return nullptr;
            }
        } else {
            if (BmhParser::Resolvable::kOut != resolvable &&
                    !formula_or_code(resolvable)) {
                parser.reportError("SK undefined");
                fAddRefFailed = true;
            }
            return nullptr;
        }
    }
    if (isupper(ref[0])) {
        auto topicIter = fBmhParser.fTopicMap.find(ref);
        if (topicIter != fBmhParser.fTopicMap.end()) {
            return topicIter->second;
        }
        size_t pos = ref.find('_');
        if (string::npos != pos) {
            // see if it is defined by another base class
            string className(ref, 0, pos);
            auto classIter = fBmhParser.fClassMap.find(className);
            if (classIter != fBmhParser.fClassMap.end()) {
                if (Definition* definition = classIter->second.find(ref,
                        RootDefinition::AllowParens::kYes)) {
                    return definition;
                }
            }
            auto enumIter = fBmhParser.fEnumMap.find(className);
            if (enumIter != fBmhParser.fEnumMap.end()) {
                if (Definition* definition = enumIter->second.find(ref,
                        RootDefinition::AllowParens::kYes)) {
                    return definition;
                }
            }
            if (BmhParser::Resolvable::kOut != resolvable &&
                    !formula_or_code(resolvable)) {
                parser.reportError("_ undefined");
                fAddRefFailed = true;
            }
            return nullptr;
        }
    }
    return nullptr;
}

string MdOut::linkName(const Definition* ref) const {
    string result = ref->fName;
    size_t under = result.find('_');
    if (string::npos != under) {
        string classPart = result.substr(0, under);
        string namePart = result.substr(under + 1, result.length());
        if (fRoot && (fRoot->fName == classPart
                || (fRoot->fParent && fRoot->fParent->fName == classPart))) {
            result = namePart;
        }
    }
    replace_all(result, "::", "_");
    return result;
}

// for now, hard-code to html links
// def should not include SkXXX_
string MdOut::linkRef(string leadingSpaces, Definition* def,
        string ref, BmhParser::Resolvable resolvable) {
    string buildup;
    string refName;
    const string* str = &def->fFiddle;
    string classPart = *str;
    bool fromInclude = "" == classPart;
    if (fromInclude) {
        const Definition* parent = def->csParent();
        SkASSERT(parent);
        classPart = parent->fName;
        refName = classPart + '_' + def->fParent->fName + '_' + ref;
    }
    SkASSERT(classPart.length() > 0);
    bool globalEnumMember = false;
    if (MarkType::kAlias == def->fMarkType) {
        def = def->fParent;
        SkASSERT(def);
        SkASSERT(MarkType::kSubtopic == def->fMarkType
            || MarkType::kTopic == def->fMarkType
            || MarkType::kConst == def->fMarkType);
    }
    if (MarkType::kSubtopic == def->fMarkType) {
        const Definition* topic = def->topicParent();
        SkASSERT(topic);
        classPart = topic->fName;
        refName = def->fName;
    } else if (MarkType::kTopic == def->fMarkType) {
        refName = def->fName;
    } else {
        if ('k' == classPart[0] && string::npos != classPart.find("_Sk")) {
            globalEnumMember = true;
        } else {
            SkASSERT("Sk" == classPart.substr(0, 2) || "SK" == classPart.substr(0, 2)
                    // FIXME: kitchen sink catch below, need to do better
                    || string::npos != def->fFileName.find("undocumented"));
            size_t under = classPart.find('_');
            if (string::npos != under) {
                classPart = classPart.substr(0, under);
            }
        }
        if (!fromInclude) {
            refName = def->fFiddle;
        }
    }
    bool classMatch = fRoot->fFileName == def->fFileName || fromInclude;
    SkASSERT(fRoot);
    SkASSERT(fRoot->fFileName.length());
    if (!classMatch) {
        string filename = def->fFileName;
        if (filename.substr(filename.length() - 4) == ".bmh") {
            filename = filename.substr(0, filename.length() - 4);
        }
        size_t start = filename.length();
        while (start > 0 && (isalnum(filename[start - 1]) || '_' == filename[start - 1])) {
            --start;
        }
        buildup = filename.substr(start);
    }
    buildup += "#" + refName;
    if (MarkType::kParam == def->fMarkType) {
        const Definition* parent = def->fParent;
        SkASSERT(MarkType::kMethod == parent->fMarkType);
        buildup = '#' + parent->fFiddle + '_' + ref;
    }
    string refOut(ref);
    if (!globalEnumMember && BmhParser::Resolvable::kCode != resolvable) {
        std::replace(refOut.begin(), refOut.end(), '_', ' ');
    }
    if (ref.length() > 2 && islower(ref[0]) && "()" == ref.substr(ref.length() - 2)
            && BmhParser::Resolvable::kCode != resolvable) {
        refOut = refOut.substr(0, refOut.length() - 2);
    }
    string result = leadingSpaces + this->anchorRef(buildup, refOut);
	if (BmhParser::Resolvable::kClone == resolvable && MarkType::kMethod == def->fMarkType &&
			def->fCloned && !def->fClone) {
		bool found = false;
		string match = def->fName;
		if ("()" == match.substr(match.length() - 2)) {
			match = match.substr(0, match.length() - 2);
		}
		match += '_';
		auto classIter = fBmhParser.fClassMap.find(classPart);
		if (fBmhParser.fClassMap.end() != classIter) {
			for (char num = '2'; num <= '9'; ++num) {
				string clone = match + num;
				const auto& leafIter = classIter->second.fLeaves.find(clone);
				if (leafIter != classIter->second.fLeaves.end()) {
					result += "<sup>" + this->anchorRef(buildup + "_" + num,
                             string("[") + num + "]") + "</sup>";
					found = true;
				}
			}
		}
		if (!found) {
			SkDebugf("");  // convenient place to set a breakpoint
		}
	}
	return result;
}

static bool writeTableEnd(MarkType markType, Definition* def, const Definition** prior) {
    return markType != def->fMarkType && *prior && markType == (*prior)->fMarkType;
}

// Recursively build string with declarative code. Skip structs, classes, that
// have been built directly.
void MdOut::addCodeBlock(const Definition* def, string& result) const {
    const Definition* last = nullptr;
    bool wroteFunction = false;
    for (auto member : def->fChildren) {
        const Definition* prior = last;
        const char* priorTerminator = nullptr;
        if (prior) {
            priorTerminator = prior->fTerminator ? prior->fTerminator : prior->fContentEnd;
        }
        last = member;
        if (KeyWord::kIfndef == member->fKeyWord) {
            this->addCodeBlock(member, result);
            continue;
        }
        if (KeyWord::kClass == member->fKeyWord || KeyWord::kStruct == member->fKeyWord
                || KeyWord::kTemplate == member->fKeyWord) {
            if (!member->fChildren.size()) {
                continue;
            }
            // todo: Make sure this was written non-elided somewhere else
            // todo: provide indent value?
            string block = fIncludeParser.elidedCodeBlock(*member);
            // add italic link for elided body
            size_t brace = block.find('{');
            if (string::npos != brace) {
                string name = member->fName;
                if ("" == name) {
                    for (auto child : member->fChildren) {
                        if ("" != (name = child->fName)) {
                            break;
                        }
                    }
                }
                SkASSERT("" != name);
                string body = "\n    // <i>" + name + " interface</i>";
                block = block.substr(0, brace + 1) + body + block.substr(brace + 1);
            }
            this->stringAppend(result, block);
            continue;
        }
        if (KeyWord::kEnum == member->fKeyWord) {
            if (member->fChildren.empty()) {
                continue;
            }
            auto tokenIter = member->fTokens.begin();
            if (KeyWord::kEnum == member->fKeyWord && KeyWord::kClass == tokenIter->fKeyWord) {
                tokenIter = tokenIter->fTokens.begin();
            }
            while (Definition::Type::kWord != tokenIter->fType) {
                std::advance(tokenIter, 1);
            }
            const auto& token = *tokenIter;
            string name = string(token.fContentStart, token.length());
            SkASSERT(name.length() > 0);
            MarkType markType = KeyWord::kClass == member->fKeyWord
                    || KeyWord::kStruct == member->fKeyWord ? MarkType::kClass : MarkType::kEnum;
            // find bmh def or just find name of class / struct / enum ? (what if enum is nameless?)
            if (wroteFunction) {
                this->stringAppend(result, '\n');
                wroteFunction = false;
            }
            this->stringAppend(result,
                    fIncludeParser.codeBlock(markType, name, fInProgress));
            this->stringAppend(result, '\n');
            continue;
        }
        // Global function declarations are not preparsed very well;
        // make do by using the prior position to find the start
        if (Bracket::kParen == member->fBracket && prior) {
            TextParser function(member->fFileName, priorTerminator, member->fTerminator + 1,
                    member->fLineCount);
            this->stringAppend(result,
                    fIncludeParser.writeCodeBlock(function, MarkType::kFunction, 0));
            this->stringAppend(result, '\n');
            wroteFunction = true;
            continue;
        }
        if (KeyWord::kTypedef == member->fKeyWord) {
            this->stringAppend(result, member);
            this->stringAppend(result, ';');
            this->stringAppend(result, '\n');
            continue;
        }
        if (KeyWord::kDefine == member->fKeyWord) {
            string body(member->fContentStart, member->length());
            if (string::npos != body.find('(')) {
                this->stringAppend(result, body);
                this->stringAppend(result, '\n');
            }
            continue;
        }
        if (KeyWord::kConstExpr == member->fKeyWord) {
            this->stringAppend(result, member);
            auto nextMember = def->fTokens.begin();
            unsigned tokenPos = member->fParentIndex + 1;
            SkASSERT(tokenPos < def->fTokens.size());
            std::advance(nextMember, tokenPos);
            while (member->fContentEnd >= nextMember->fContentStart) {
                std::advance(nextMember, 1);
                SkASSERT(++tokenPos < def->fTokens.size());
            }
            while (Punctuation::kSemicolon != nextMember->fPunctuation) {
                std::advance(nextMember, 1);
                SkASSERT(++tokenPos < def->fTokens.size());
            }
            TextParser between(member->fFileName, member->fContentEnd,
                    nextMember->fContentStart, member->fLineCount);
            between.skipWhiteSpace();
            if ('=' == between.peek()) {
                this->stringAppend(result, ' ');
                string middle(between.fChar, nextMember->fContentStart);
                this->stringAppend(result, middle);
                last = nullptr;
            } else {
                SkAssertResult(';' == between.peek());
            }
            this->stringAppend(result, ';');
            this->stringAppend(result, '\n');
            continue;
        }
    }
}

void MdOut::markTypeOut(Definition* def, const Definition** prior) {
    string printable = def->printableName();
    const char* textStart = def->fContentStart;
    bool lookForOneLiner = false;
    // #Param and #Const don't have markers to say when the last is seen, so detect that by looking
    // for a change in type.
    if (writeTableEnd(MarkType::kParam, def, prior) || writeTableEnd(MarkType::kConst, def, prior)
                || writeTableEnd(MarkType::kMember, def, prior)) {
        this->writePending();
        FPRINTF("</table>");
        this->lf(2);
        fTableState = TableState::kNone;
    }
    fLastDef = def;
    switch (def->fMarkType) {
        case MarkType::kAlias:
            break;
        case MarkType::kAnchor: {
            if (fColumn > 0) {
                this->writeSpace();
            }
            this->writePending();
            TextParser parser(def);
            const char* start = parser.fChar;
            parser.skipToEndBracket((string(" ") + def->fMC + " ").c_str());
            string anchorText(start, parser.fChar - start);
            parser.skipExact((string(" ") + def->fMC + " ").c_str());
            string anchorLink(parser.fChar, parser.fEnd - parser.fChar);
            this->htmlOut(anchorRef(anchorLink, anchorText));
            } break;
        case MarkType::kBug:
            break;
        case MarkType::kClass:
        case MarkType::kStruct:
            fRoot = def->asRoot();
            this->lfAlways(2);
            if (MarkType::kStruct == def->fMarkType) {
                this->htmlOut(anchorDef(def->fFiddle, ""));
            } else {
                this->htmlOut(anchorDef(this->linkName(def), ""));
            }
            this->lfAlways(2);
            FPRINTF("---");
            this->lf(2);
            break;
        case MarkType::kCode:
            this->lfAlways(2);
            FPRINTF("<pre style=\"padding: 1em 1em 1em 1em;"
                    "width: 62.5em; background-color: #f0f0f0\">");
            this->lf(1);
            fResolveAndIndent = true;
            break;
        case MarkType::kColumn:
            this->writePending();
            if (fInList) {
                FPRINTF("    <td>");
            } else {
                FPRINTF("| ");
            }
            break;
        case MarkType::kComment:
            break;
        case MarkType::kMember:
        case MarkType::kConst: {
            bool isConst = MarkType::kConst == def->fMarkType;
            lookForOneLiner = false;
            fWroteSomething = false;
        // output consts for one parent with moderate descriptions
        // optional link to subtopic with longer descriptions, examples
            if (TableState::kNone == fTableState) {
                SkASSERT(!*prior || (isConst && MarkType::kConst != (*prior)->fMarkType)
                        || (!isConst && MarkType::kMember != (*prior)->fMarkType));
                if (isConst) {
                    this->mdHeaderOut(3);
                    this->writeString(this->fPopulators[SubtopicKeys::kConstants].fPlural);
                    this->lfAlways(2);
                }
                FPRINTF("%s", kTableDeclaration);
                fTableState = TableState::kRow;
                fOddRow = true;
                this->lfAlways(1);
                // look ahead to see if the details column has data or not
                fHasDetails = MdOut::HasDetails(def->fParent);
                FPRINTF("%s", fHasDetails ? \
                        (isConst ? kSubConstTableHeader : kSubMemberTableHeader) : \
                        (isConst ? kAllConstTableHeader : kAllMemberTableHeader));
                this->lfAlways(1);
            }
            if (TableState::kRow == fTableState) {
                this->writePending();
                FPRINTF("%s", fOddRow ? kTR_Dark.c_str() : "  <tr>");
                fOddRow = !fOddRow;
                this->lfAlways(1);
                fTableState = TableState::kColumn;
            }
            this->writePending();
            if (isConst) {
                // TODO: if fHasDetails is true, could defer def and issue a ref instead
                // unclear if this is a good idea or not
                FPRINTF("%s", this->tableDataCodeDef(def).c_str());
                this->lfAlways(1);
                FPRINTF("%s", table_data_const(def, &textStart).c_str());
            } else {
                string memberType;
                string memberName = this->getMemberTypeName(def, &memberType);
                FPRINTF("%s", out_table_data_description(memberType).c_str());
                this->lfAlways(1);
                FPRINTF("%s", tableDataCodeDef(def->fFiddle, memberName).c_str());
            }
            this->lfAlways(1);
            if (fHasDetails) {
                string details;
                auto subtopic = std::find_if(def->fChildren.begin(), def->fChildren.end(),
                        [](const Definition* test){
                        return MarkType::kDetails == test->fMarkType; } );
                if (def->fChildren.end() != subtopic) {
                    string subtopicName = string((*subtopic)->fContentStart,
                            (int) ((*subtopic)->fContentEnd - (*subtopic)->fContentStart));
                    const Definition* parentSubtopic = def->subtopicParent();
                    SkASSERT(parentSubtopic);
                    string fullName = parentSubtopic->fFiddle + '_' + subtopicName;
                    if (fBmhParser.fTopicMap.end() == fBmhParser.fTopicMap.find(fullName)) {
                        (*subtopic)->reportError<void>("missing #Details subtopic");
                    }
             //       subtopicName = parentSubtopic->fName + '_' + subtopicName;
                    string noUnderscores = subtopicName;
                    replace_all(noUnderscores, "_", "&nbsp;");
                    details = this->anchorLocalRef(subtopicName, noUnderscores) + "&nbsp;";
                }
                FPRINTF("%s", out_table_data_details(details).c_str());
                this->lfAlways(1);
            }
            lookForOneLiner = true;  // if description is empty, use oneLiner data
            FPRINTF("%s", out_table_data_description_start().c_str()); // start of Description
            this->lfAlways(1);
        } break;
        case MarkType::kDeprecated:
            this->writeString(def->fParent->incompleteMessage(
                    Definition::DetailsType::kSentence).c_str());
            this->lf(2);
            break;
        case MarkType::kDescription:
            fInDescription = true;
            this->writePending();
            FPRINTF("%s", "<div>");
            break;
        case MarkType::kDetails:
            break;
        case MarkType::kDuration:
            break;
        case MarkType::kDefine:
        case MarkType::kEnum:
        case MarkType::kEnumClass:
            this->lfAlways(2);
            this->htmlOut(anchorDef(def->fFiddle, ""));
            this->lfAlways(2);
            FPRINTF("---");
            this->lf(2);
            break;
        case MarkType::kExample: {
            this->mdHeaderOut(3);
            FPRINTF("%s", "Example\n"
                            "\n");
            fHasFiddle = true;
            bool showGpu = false;
            bool gpuAndCpu = false;
            const Definition* platform = def->hasChild(MarkType::kPlatform);
            if (platform) {
                TextParser platParse(platform);
                fHasFiddle = !platParse.strnstr("!fiddle", platParse.fEnd);
                showGpu = platParse.strnstr("gpu", platParse.fEnd);
                if (showGpu) {
                    gpuAndCpu = platParse.strnstr("cpu", platParse.fEnd);
                }
            }
            if (fHasFiddle) {
                SkASSERT(def->fHash.length() > 0);
                FPRINTF("<div><fiddle-embed name=\"%s\"", def->fHash.c_str());
                if (showGpu) {
                    FPRINTF("%s", " gpu=\"true\"");
                    if (gpuAndCpu) {
                        FPRINTF("%s", " cpu=\"true\"");
                    }
                }
                FPRINTF("%s", ">");
            } else {
                SkASSERT(def->fHash.length() == 0);
                FPRINTF("%s", "<pre style=\"padding: 1em 1em 1em 1em; font-size: 13px"
                        " width: 62.5em; background-color: #f0f0f0\">");
                this->lfAlways(1);
                if (def->fWrapper.length() > 0) {
                    FPRINTF("%s", def->fWrapper.c_str());
                }
                fLiteralAndIndent = true;
            }
            } break;
        case MarkType::kExperimental:
            this->writeString(def->fParent->incompleteMessage(
                    Definition::DetailsType::kSentence).c_str());
            this->lf(2);
            break;
        case MarkType::kExternal:
            break;
        case MarkType::kFile:
            break;
        case MarkType::kFilter:
            break;
        case MarkType::kFormula:
            break;
        case MarkType::kFunction:
            break;
        case MarkType::kHeight:
            break;
        case MarkType::kIllustration: {
            string illustName = "Illustrations_" + def->fParent->fFiddle;
            string number = string(def->fContentStart, def->length());
            if (number.length() && "1" != number) {
                illustName += "_" + number;
            }
            auto illustIter = fBmhParser.fTopicMap.find(illustName);
            SkASSERT(fBmhParser.fTopicMap.end() != illustIter);
            Definition* illustDef = illustIter->second;
            SkASSERT(MarkType::kSubtopic == illustDef->fMarkType);
            SkASSERT(1 == illustDef->fChildren.size());
            Definition* illustExample = illustDef->fChildren[0];
            SkASSERT(MarkType::kExample == illustExample->fMarkType);
            string hash = illustExample->fHash;
            SkASSERT("" != hash);
            string title;
            this->writePending();
            FPRINTF("![%s](https://fiddle.skia.org/i/%s_raster.png \"%s\")",
                    def->fName.c_str(), hash.c_str(), title.c_str());
            this->lf(2);
        } break;
        case MarkType::kImage:
            break;
        case MarkType::kIn:
            break;
        case MarkType::kLegend:
            break;
        case MarkType::kLine:
            break;
        case MarkType::kLink:
            break;
        case MarkType::kList:
            fInList = true;
            fTableState = TableState::kRow;
            this->lfAlways(2);
            FPRINTF("%s", "<table>");
            this->lf(1);
            break;
        case MarkType::kLiteral:
            break;
        case MarkType::kMarkChar:
            fBmhParser.fMC = def->fContentStart[0];
            break;
        case MarkType::kMethod: {
            this->lfAlways(2);
			if (false && !def->isClone()) {
                string method_name = def->methodName();
                this->mdHeaderOutLF(2, 1);
                this->htmlOut(this->anchorDef(def->fFiddle, method_name));
			} else {
                this->htmlOut(this->anchorDef(def->fFiddle, ""));
            }
            this->lfAlways(2);
            FPRINTF("---");
			this->lf(2);

            // TODO: put in css spec that we can define somewhere else (if markup supports that)
            // TODO: 50em below should match limit = 80 in formatFunction()
            this->writePending();
            string formattedStr = def->formatFunction(Definition::Format::kIncludeReturn);
            string preformattedStr = preformat(formattedStr);
            string references = this->addReferences(&preformattedStr.front(),
                    &preformattedStr.back() + 1, BmhParser::Resolvable::kSimple);
            preformattedStr = references;
            this->htmlOut("<pre style=\"padding: 1em 1em 1em 1em; width: 62.5em;"
                    "background-color: #f0f0f0\">\n" + preformattedStr + "\n" + "</pre>");
            this->lf(2);
            fTableState = TableState::kNone;
            fMethod = def;
            } break;
        case MarkType::kNoExample:
            break;
        case MarkType::kNoJustify:
            break;
        case MarkType::kOutdent:
            break;
        case MarkType::kParam: {
            TextParser paramParser(def->fFileName, def->fStart, def->fContentStart,
                    def->fLineCount);
            paramParser.skipWhiteSpace();
            SkASSERT(paramParser.startsWith("#Param"));
            paramParser.next(); // skip hash
            paramParser.skipToNonName(); // skip Param
            this->parameterHeaderOut(paramParser, prior, def);
        } break;
        case MarkType::kPhraseDef:
            // skip text and children
            *prior = def;
            return;
        case MarkType::kPhraseParam:
            SkDebugf(""); // convenient place to set a breakpoint
            break;
        case MarkType::kPhraseRef:
            if (fPhraseParams.end() != fPhraseParams.find(def->fName)) {
                if (fColumn > 0) {
                    this->writeSpace();
                }
                this->writeString(fPhraseParams[def->fName]);
                if (isspace(def->fContentStart[0])) {
                    this->writeSpace();
                }
            } else if (fBmhParser.fPhraseMap.end() == fBmhParser.fPhraseMap.find(def->fName)) {
                def->reportError<void>("missing phrase definition");
                fAddRefFailed = true;
            } else {
                if (fColumn) {
                    SkASSERT(' ' >= def->fStart[0]);
                    this->writeSpace();
                }
                Definition* phraseRef = fBmhParser.fPhraseMap.find(def->fName)->second;
                // def->fChildren are parameters to substitute phraseRef->fChildren,
                // phraseRef->fChildren has both param defines and references
                // def->fChildren must have the same number of entries as phaseRef->fChildren
                // which are kPhraseParam, and substitute one for one
                // Then, each kPhraseRef in phaseRef looks up the key and value
                fPhraseParams.clear();
                auto refKidsIter = phraseRef->fChildren.begin();
                for (auto child : def->fChildren) {
                    if (MarkType::kPhraseParam != child->fMarkType) {
                        // more work to do to support other types
                        this->reportError("phrase ref child must be param");
                    }
                    do {
                        if (refKidsIter == phraseRef->fChildren.end()) {
                            this->reportError("phrase def missing param");
                            break;
                        }
                        if (MarkType::kPhraseRef == (*refKidsIter)->fMarkType) {
                            continue;
                        }
                        if (MarkType::kPhraseParam != (*refKidsIter)->fMarkType) {
                            this->reportError("unexpected type in phrase def children");
                            break;
                        }
                        fPhraseParams[(*refKidsIter)->fName] = child->fName;
                        break;
                    } while (true);
                }
                this->childrenOut(phraseRef, phraseRef->fContentStart);
                fPhraseParams.clear();
                if (' ' >= def->fContentStart[0] && !fPendingLF) {
                    this->writeSpace();
                }
            }
            break;
        case MarkType::kPlatform:
            break;
        case MarkType::kPopulate: {
            Definition* parent = def->fParent;
            SkASSERT(parent);
            if (MarkType::kCode == parent->fMarkType) {
                auto inDef = std::find_if(parent->fChildren.begin(), parent->fChildren.end(),
                        [](const Definition* child) { return MarkType::kIn == child->fMarkType; });
                if (parent->fChildren.end() != inDef) {
                    auto filterDef = std::find_if(parent->fChildren.begin(),
                            parent->fChildren.end(), [](const Definition* child) {
                            return MarkType::kFilter == child->fMarkType; });
                    SkASSERT(parent->fChildren.end() != filterDef);
                    string codeBlock = fIncludeParser.filteredBlock(
                            string((*inDef)->fContentStart, (*inDef)->length()),
                            string((*filterDef)->fContentStart, (*filterDef)->length()));
                    this->resolveOut(codeBlock.c_str(), codeBlock.c_str() + codeBlock.length(),
                            this->resolvable(parent));
                    break;
                }
                // find include matching code parent
                Definition* grand = parent->fParent;
                SkASSERT(grand);
                if (MarkType::kClass == grand->fMarkType
                        || MarkType::kStruct == grand->fMarkType
                        || MarkType::kEnum == grand->fMarkType
                        || MarkType::kEnumClass == grand->fMarkType
                        || MarkType::kTypedef == grand->fMarkType
                        || MarkType::kDefine == grand->fMarkType) {
                    string codeBlock = fIncludeParser.codeBlock(*grand, fInProgress);
                    this->resolveOut(codeBlock.c_str(), codeBlock.c_str() + codeBlock.length(),
                            this->resolvable(parent));
                } else if (MarkType::kTopic == grand->fMarkType) {
                    // use bmh file name to find include file name
                    size_t start = grand->fFileName.rfind("Sk");
                    SkASSERT(start != string::npos);
                    size_t end = grand->fFileName.rfind("_Reference");
                    SkASSERT(end != string::npos && end > start);
                    string incName(grand->fFileName.substr(start, end - start));
                    const Definition* includeDef = fIncludeParser.include(incName + ".h");
                    SkASSERT(includeDef);
                    string codeBlock;
                    this->addCodeBlock(includeDef, codeBlock);
                    this->resolveOut(codeBlock.c_str(), codeBlock.c_str() + codeBlock.length(),
                            this->resolvable(parent));
                } else {
                    SkASSERT(MarkType::kSubtopic == grand->fMarkType);
                    auto inTag = std::find_if(grand->fChildren.begin(), grand->fChildren.end(),
                            [](Definition* child){return MarkType::kIn == child->fMarkType;});
                    SkASSERT(grand->fChildren.end() != inTag);
                    auto filterTag = std::find_if(grand->fChildren.begin(), grand->fChildren.end(),
                            [](Definition* child){return MarkType::kFilter == child->fMarkType;});
                    SkASSERT(grand->fChildren.end() != filterTag);
                    string inContents((*inTag)->fContentStart, (*inTag)->length());
                    string filterContents((*filterTag)->fContentStart, (*filterTag)->length());
                    string filteredBlock = fIncludeParser.filteredBlock(inContents, filterContents);
                    this->resolveOut(filteredBlock.c_str(), filteredBlock.c_str()
                            + filteredBlock.length(), this->resolvable(parent));
                }
            } else {
                SkASSERT(MarkType::kMethod == parent->fMarkType);
                // retrieve parameters, return, description from include
                Definition* iMethod = fIncludeParser.findMethod(*parent);
                SkDebugf("");
                bool wroteParam = false;
                fMethod = iMethod;
                for (auto& entry : iMethod->fTokens) {
                    if (MarkType::kComment != entry.fMarkType) {
                        continue;
                    }
                    TextParser parser(&entry);
                    if (parser.skipExact("@param ")) { // write parameters, if any
                        this->parameterHeaderOut(parser, prior, def);
                        this->resolveOut(parser.fChar, parser.fEnd, BmhParser::Resolvable::kYes);
                        this->parameterTrailerOut();
                        wroteParam = true;
                        continue;
                    }
                    if (wroteParam) {
                        this->writePending();
                        FPRINTF("</table>");
                        this->lf(2);
                        fTableState = TableState::kNone;
                        wroteParam = false;
                    }
                    if (parser.skipExact("@return ")) { // write return, if any
                        this->returnHeaderOut(prior, def);
                        this->resolveOut(parser.fChar, parser.fEnd, BmhParser::Resolvable::kYes);
                        this->lf(2);
                        continue;
                    }
                    if (1 == entry.length() && '/' == entry.fContentStart[0]) {
                        continue;
                    }
                    this->resolveOut(entry.fContentStart, entry.fContentEnd,
                            BmhParser::Resolvable::kYes);  // write description
                    this->lf(1);
                }
                fMethod = nullptr;
            }
            } break;
        case MarkType::kPrivate:
            this->writeString("Private:");
            this->writeSpace();
            this->writeBlock(def->length(), def->fContentStart);
            this->lf(2);
            break;
        case MarkType::kReturn:
            this->returnHeaderOut(prior, def);
            break;
        case MarkType::kRow:
            if (fInList) {
                FPRINTF("  <tr>");
                this->lf(1);
            }
            break;
        case MarkType::kSeeAlso:
            this->mdHeaderOut(3);
            FPRINTF("See Also");
            this->lf(2);
            break;
        case MarkType::kSet:
            break;
        case MarkType::kStdOut: {
            TextParser code(def);
            this->mdHeaderOut(4);
            FPRINTF(
                    "Example Output\n"
                    "\n"
                    "~~~~");
            this->lfAlways(1);
            code.skipSpace();
            while (!code.eof()) {
                const char* end = code.trimmedLineEnd();
                FPRINTF("%.*s\n", (int) (end - code.fChar), code.fChar);
                code.skipToLineStart();
            }
            FPRINTF("~~~~");
            this->lf(2);
            } break;
        case MarkType::kSubstitute:
            break;
        case MarkType::kSubtopic:
            fSubtopic = def->asRoot();
            if (false && SubtopicKeys::kOverview == def->fName) {
                this->writeString(def->fName);
            } else {
                this->lfAlways(2);
                this->htmlOut(anchorDef(def->fName, ""));
            }
            if (std::any_of(def->fChildren.begin(), def->fChildren.end(),
                    [](Definition* child) {
                    return MarkType::kSeeAlso == child->fMarkType
                    || MarkType::kExample == child->fMarkType
                    || MarkType::kNoExample == child->fMarkType;
            })) {
                this->lfAlways(2);
                FPRINTF("---");
            }
            this->lf(2);
#if 0
            // if a subtopic child is const, generate short table of const name, value, line desc
            if (std::any_of(def->fChildren.begin(), def->fChildren.end(),
                    [](Definition* child){return MarkType::kConst == child->fMarkType;})) {
                this->summaryOut(def, MarkType::kConst, fPopulators[SubtopicKeys::kConstants].fPlural);
            }
#endif
            // if a subtopic child is member, generate short table of const name, value, line desc
            if (std::any_of(def->fChildren.begin(), def->fChildren.end(),
                    [](Definition* child){return MarkType::kMember == child->fMarkType;})) {
                this->summaryOut(def, MarkType::kMember, fPopulators[SubtopicKeys::kMembers].fPlural);
            }
            break;
        case MarkType::kTable:
            this->lf(2);
            break;
        case MarkType::kTemplate:
            break;
        case MarkType::kText:
            if (def->fParent && MarkType::kFormula == def->fParent->fMarkType) {
                if (fColumn > 0) {
                    this->writeSpace();
                }
                this->writePending();
                this->htmlOut("<code>");
                this->resolveOut(def->fContentStart, def->fContentEnd,
                        BmhParser::Resolvable::kFormula);
                this->htmlOut("</code>");
            }
            break;
        case MarkType::kToDo:
            break;
        case MarkType::kTopic: {
            auto found = std::find_if(def->fChildren.begin(), def->fChildren.end(),
                    [](Definition* test) { return test->isStructOrClass(); } );
            bool hasClassOrStruct = def->fChildren.end() != found;
            fRoot = hasClassOrStruct ? (*found)->asRoot() : def->asRoot();
            fSubtopic = def->asRoot();
            bool isUndocumented = string::npos != def->fFileName.find("undocumented");
            if (!isUndocumented) {
                this->populateTables(def, fRoot);
            }
//            this->mdHeaderOut(1);
//            this->htmlOut(anchorDef(this->linkName(def), printable));
//            this->lf(1);
            } break;
        case MarkType::kTypedef:
            this->lfAlways(2);
            this->htmlOut(anchorDef(def->fFiddle, ""));
            this->lfAlways(2);
            FPRINTF("---");
            this->lf(2);
            break;
        case MarkType::kUnion:
            break;
        case MarkType::kVolatile:
            break;
        case MarkType::kWidth:
            break;
        default:
            SkDebugf("fatal error: MarkType::k%s unhandled in %s()\n",
                    BmhParser::kMarkProps[(int) def->fMarkType].fName, __func__);
            SkASSERT(0); // handle everything
            break;
    }
    this->childrenOut(def, textStart);
    switch (def->fMarkType) {  // post child work, at least for tables
        case MarkType::kAnchor:
            if (fColumn > 0) {
                this->writeSpace();
            }
            break;
        case MarkType::kClass:
        case MarkType::kStruct:
            if (TableState::kNone != fTableState) {
                this->writePending();
                FPRINTF("</table>");
                this->lf(2);
                fTableState = TableState::kNone;
            }
            if (def->csParent()) {
                fRoot = def->csParent()->asRoot();
            }
            break;
        case MarkType::kCode:
            fIndent = 0;
            this->lf(1);
            this->writePending();
            FPRINTF("</pre>");
            this->lf(2);
            fResolveAndIndent = false;
            break;
        case MarkType::kColumn:
            if (fInList) {
                this->writePending();
                FPRINTF("</td>");
                this->lfAlways(1);
            } else {
                FPRINTF(" ");
            }
            break;
        case MarkType::kDescription:
            this->writePending();
            FPRINTF("</div>");
            fInDescription = false;
            break;
        case MarkType::kEnum:
        case MarkType::kEnumClass:
            if (TableState::kNone != fTableState) {
                this->writePending();
                FPRINTF("</table>");
                this->lf(2);
                fTableState = TableState::kNone;
            }
            break;
        case MarkType::kExample:
            this->writePending();
            if (fHasFiddle) {
                FPRINTF("</fiddle-embed></div>");
            } else {
                this->lfAlways(1);
                if (def->fWrapper.length() > 0) {
                    FPRINTF("}");
                    this->lfAlways(1);
                }
                FPRINTF("</pre>");
            }
            this->lf(2);
            fLiteralAndIndent = false;
            break;
        case MarkType::kLink:
            this->writeString("</a>");
            this->writeSpace();
            break;
        case MarkType::kList:
            fInList = false;
            this->writePending();
            SkASSERT(TableState::kNone != fTableState);
            FPRINTF("</table>");
            this->lf(2);
            fTableState = TableState::kNone;
            break;
        case MarkType::kLegend: {
            SkASSERT(def->fChildren.size() == 1);
            const Definition* row = def->fChildren[0];
            SkASSERT(MarkType::kRow == row->fMarkType);
            size_t columnCount = row->fChildren.size();
            SkASSERT(columnCount > 0);
            this->writePending();
            for (size_t index = 0; index < columnCount; ++index) {
                FPRINTF("| --- ");
            }
            FPRINTF(" |");
            this->lf(1);
            } break;
        case MarkType::kMethod:
            fMethod = nullptr;
            break;
        case MarkType::kConst:
        case MarkType::kMember:
            if (lookForOneLiner && !fWroteSomething) {
                auto oneLiner = std::find_if(def->fChildren.begin(), def->fChildren.end(),
                        [](const Definition* test){ return MarkType::kLine == test->fMarkType; } );
                if (def->fChildren.end() != oneLiner) {
                    TextParser parser(*oneLiner);
                    parser.skipWhiteSpace();
                    parser.trimEnd();
                    FPRINTF("%.*s", (int) (parser.fEnd - parser.fChar), parser.fChar);
                }
                lookForOneLiner = false;
            }
        case MarkType::kParam:
            this->parameterTrailerOut();
            break;
        case MarkType::kReturn:
        case MarkType::kSeeAlso:
            this->lf(2);
            break;
        case MarkType::kRow:
            if (fInList) {
                FPRINTF("  </tr>");
            } else {
                FPRINTF("|");
            }
            this->lf(1);
            break;
        case MarkType::kTable:
            this->lf(2);
            break;
        case MarkType::kPhraseDef:
            break;
        case MarkType::kPrivate:
            break;
        case MarkType::kSubtopic:
            SkASSERT(def);
            do {
                def = def->fParent;
            } while (def && MarkType::kTopic != def->fMarkType
                    && MarkType::kSubtopic != def->fMarkType);
            SkASSERT(def);
            fSubtopic = def->asRoot();
            break;
        case MarkType::kTopic:
            fSubtopic = nullptr;
            break;
        default:
            break;
    }
    *prior = def;
}

void MdOut::mdHeaderOutLF(int depth, int lf) {
    this->lfAlways(lf);
    for (int index = 0; index < depth; ++index) {
        FPRINTF("#");
    }
    FPRINTF(" ");
}

void MdOut::parameterHeaderOut(TextParser& paramParser, const Definition** prior, Definition* def) {
    if (TableState::kNone == fTableState) {
        SkASSERT(!*prior || MarkType::kParam != (*prior)->fMarkType);
        this->mdHeaderOut(3);
        this->htmlOut(
                "Parameters\n"
                "\n"
                "<table>"
                );
        this->lf(1);
        fTableState = TableState::kRow;
    }
    if (TableState::kRow == fTableState) {
        FPRINTF("  <tr>");
        this->lf(1);
        fTableState = TableState::kColumn;
    }
    paramParser.skipSpace();
    const char* paramName = paramParser.fChar;
    paramParser.skipToSpace();
    string paramNameStr(paramName, (int) (paramParser.fChar - paramName));
    if (MarkType::kPopulate != def->fMarkType && !this->checkParamReturnBody(def)) {
        *prior = def;
        return;
    }
    string refNameStr = def->fParent->fFiddle + "_" + paramNameStr;
    this->htmlOut("    <td>" + this->anchorDef(refNameStr,
            "<code><strong>" + paramNameStr + "</strong></code>") + "</td>");
    this->lfAlways(1);
    FPRINTF("    <td>");
}

void MdOut::parameterTrailerOut() {
    SkASSERT(TableState::kColumn == fTableState);
    fTableState = TableState::kRow;
    this->writePending();
    FPRINTF("</td>");
    this->lfAlways(1);
    FPRINTF("  </tr>");
    this->lfAlways(1);
}

void MdOut::populateOne(Definition* def,
        unordered_map<string, RootDefinition::SubtopicContents>& populator) {
    if (MarkType::kConst == def->fMarkType) {
        populator[SubtopicKeys::kConstants].fMembers.push_back(def);
        return;
    }
    if (MarkType::kEnum == def->fMarkType || MarkType::kEnumClass == def->fMarkType) {
        populator[SubtopicKeys::kConstants].fMembers.push_back(def);
        return;
    }
    if (MarkType::kDefine == def->fMarkType) {
        populator[SubtopicKeys::kDefines].fMembers.push_back(def);
        return;
    }
    if (MarkType::kMember == def->fMarkType) {
        populator[SubtopicKeys::kMembers].fMembers.push_back(def);
        return;
    }
    if (MarkType::kTypedef == def->fMarkType) {
        populator[SubtopicKeys::kTypedefs].fMembers.push_back(def);
        return;
    }
    if (MarkType::kMethod != def->fMarkType) {
        return;
    }
    if (def->fClone) {
        return;
    }
    if (Definition::MethodType::kConstructor == def->fMethodType
            || Definition::MethodType::kDestructor == def->fMethodType) {
        populator[SubtopicKeys::kConstructors].fMembers.push_back(def);
        return;
    }
    if (Definition::MethodType::kOperator == def->fMethodType) {
        populator[SubtopicKeys::kOperators].fMembers.push_back(def);
        return;
    }
    populator[SubtopicKeys::kMemberFunctions].fMembers.push_back(def);
    const Definition* csParent = this->csParent();
    if (csParent) {
        if (0 == def->fName.find(csParent->fName + "::Make")
                || 0 == def->fName.find(csParent->fName + "::make")) {
            populator[SubtopicKeys::kConstructors].fMembers.push_back(def);
            return;
        }
    }
    for (auto item : def->fChildren) {
        if (MarkType::kIn == item->fMarkType) {
            string name(item->fContentStart, item->fContentEnd - item->fContentStart);
            populator[name].fMembers.push_back(def);
            populator[name].fShowClones = true;
            break;
        }
    }
}

void MdOut::populateTables(const Definition* def, RootDefinition* root) {
    for (auto child : def->fChildren) {
        if (MarkType::kSubtopic == child->fMarkType) {
            string name = child->fName;
            bool builtInTopic = name == SubtopicKeys::kOverview;
            for (auto item : SubtopicKeys::kGeneratedSubtopics) {
                builtInTopic |= name == item;
            }
            if (!builtInTopic) {
                string subname;
                const Definition* subtopic = child->subtopicParent();
                if (subtopic) {
                    subname = subtopic->fName + '_';
                }
                builtInTopic = name == subname + SubtopicKeys::kOverview;
                for (auto item : SubtopicKeys::kGeneratedSubtopics) {
                    builtInTopic |= name == subname + item;
                }
                if (!builtInTopic) {
                    root->populator(SubtopicKeys::kRelatedFunctions).fMembers.push_back(child);
                }
            }
            this->populateTables(child, root);
            continue;
        }
        if (child->isStructOrClass()) {
            if (fClassStack.size() > 0) {
                root->populator(MarkType::kStruct != child->fMarkType ? SubtopicKeys::kClasses :
                        SubtopicKeys::kStructs).fMembers.push_back(child);
            }
            fClassStack.push_back(child);
            this->populateTables(child, child->asRoot());
            fClassStack.pop_back();
            continue;
        }
        if (MarkType::kEnum == child->fMarkType || MarkType::kEnumClass == child->fMarkType) {
            this->populateTables(child, root);
        }
        this->populateOne(child, root->fPopulators);
    }
}

void MdOut::resolveOut(const char* start, const char* end, BmhParser::Resolvable resolvable) {
    if ((BmhParser::Resolvable::kLiteral == resolvable || fLiteralAndIndent ||
            fResolveAndIndent) && end > start) {
        int linefeeds = 0;
        while ('\n' == *start) {
            ++linefeeds;
            ++start;
        }
        if (fResolveAndIndent && linefeeds) {
            this->lf(linefeeds);
        }
        const char* spaceStart = start;
        while (' ' == *start) {
            ++start;
        }
        if (start > spaceStart) {
            fIndent = start - spaceStart;
        }
    }
    if (BmhParser::Resolvable::kLiteral == resolvable || fLiteralAndIndent) {
        this->writeBlockTrim(end - start, start);
        if ('\n' == end[-1]) {
            this->lf(1);
        }
        fIndent = 0;
        return;
    }
    // FIXME: this needs the markdown character present when the def was defined,
    // not the last markdown character the parser would have seen...
    while (fBmhParser.fMC == end[-1]) {
        --end;
    }
    if (start >= end) {
        return;
    }
    string resolved = this->addReferences(start, end, resolvable);
    trim_end_spaces(resolved);
    if (resolved.length()) {
        TextParser paragraph(fFileName, &*resolved.begin(), &*resolved.end(), fLineCount);
        while (!paragraph.eof()) {
            while ('\n' == paragraph.peek()) {
                paragraph.next();
                if (paragraph.eof()) {
                    return;
                }
            }
            const char* lineStart = paragraph.fChar;
            paragraph.skipWhiteSpace();
            const char* contentStart = paragraph.fChar;
            if (fResolveAndIndent && contentStart > lineStart) {
                this->writePending();
                this->indentToColumn(contentStart - lineStart);
            }
            paragraph.skipToEndBracket('\n');
            ptrdiff_t lineLength = paragraph.fChar - contentStart;
            if (lineLength) {
                while (lineLength && contentStart[lineLength - 1] <= ' ') {
                    --lineLength;
                }
                string str(contentStart, lineLength);
                this->writeString(str.c_str());
                fWroteSomething = !!lineLength;
            }
            if (paragraph.eof()) {
                break;
            }
            if ('\n' == paragraph.next()) {
                int linefeeds = 1;
                if (!paragraph.eof() && '\n' == paragraph.peek()) {
                    linefeeds = 2;
                }
                this->lf(linefeeds);
            }
        }
    }
}

void MdOut::returnHeaderOut(const Definition** prior, Definition* def) {
    this->mdHeaderOut(3);
    FPRINTF("Return Value");
    if (MarkType::kPopulate != def->fMarkType && !this->checkParamReturnBody(def)) {
        *prior = def;
        return;
    }
    this->lf(2);
}

void MdOut::rowOut(string col1, const Definition* col2) {
    FPRINTF("%s", fOddRow ? kTR_Dark.c_str() : "  <tr>");
    this->lfAlways(1);
    FPRINTF("%s", kTD_Left.c_str());
    if ("" != col1) {
        this->writeString(col1);
    }
    FPRINTF("</td>");
    this->lfAlways(1);
    FPRINTF("%s", kTD_Left.c_str());
    TextParser parser(col2->fFileName, col2->fStart, col2->fContentStart, col2->fLineCount);
    parser.skipExact("#Method");
    parser.skipSpace();
    parser.trimEnd();
    string methodName(parser.fChar, parser.fEnd - parser.fChar);
    this->htmlOut(this->anchorRef("#" + col2->fFiddle, methodName));
    this->htmlOut("</td>");
    this->lfAlways(1);
    FPRINTF("  </tr>");
    this->lfAlways(1);
    fOddRow = !fOddRow;
}

void MdOut::rowOut(const char* name, string description, bool literalName) {
    FPRINTF("%s", fOddRow ? kTR_Dark.c_str() : "  <tr>");
    this->lfAlways(1);
    FPRINTF("%s", kTD_Left.c_str());
    if (literalName) {
        if (strlen(name)) {
            this->writeString(name);
        }
    } else {
        this->resolveOut(name, name + strlen(name), BmhParser::Resolvable::kYes);
    }
    FPRINTF("</td>");
    this->lfAlways(1);
    FPRINTF("%s", kTD_Left.c_str());
    this->resolveOut(&description.front(), &description.back() + 1, BmhParser::Resolvable::kYes);
    FPRINTF("</td>");
    this->lfAlways(1);
    FPRINTF("  </tr>");
    this->lfAlways(1);
    fOddRow = !fOddRow;
}

void MdOut::subtopicsOut(Definition* def) {
    Definition* csParent = def->csParent();
    const Definition* subtopicParent = def->subtopicParent();
    const Definition* topicParent = def->topicParent();
    SkASSERT(subtopicParent);
    this->lfAlways(1);
    FPRINTF("%s", kTableDeclaration);
    this->lfAlways(1);
    FPRINTF("%s", kTopicsTableHeader);
    this->lfAlways(1);
    fOddRow = true;
    for (auto item : SubtopicKeys::kGeneratedSubtopics) {
        if (SubtopicKeys::kMemberFunctions == item) {
            continue;
        }
        for (auto entry : fRoot->populator(item).fMembers) {
            if ((csParent && entry->csParent() == csParent)
                    || entry->subtopicParent() == subtopicParent) {
                if (SubtopicKeys::kRelatedFunctions == item) {
                    (void) subtopicRowOut(entry->fName, entry); // report all errors
                    continue;
                }
                auto popItem = fPopulators.find(item);
                string description = popItem->second.fOneLiner;
                if (SubtopicKeys::kConstructors == item) {
                    description += " " + fRoot->fName;
                }
                string subtopic;
                if (subtopicParent != topicParent) {
                    subtopic = subtopicParent->fName + '_';
                }
                string link = this->anchorLocalRef(subtopic + item, popItem->second.fPlural);
                this->rowOut(link.c_str(), description, true);
                break;
            }
        }
    }
    FPRINTF("</table>");
    this->lfAlways(1);
}

void MdOut::subtopicOut(string name) {
    const Definition* topicParent = fSubtopic ? fSubtopic->topicParent() : nullptr;
    Definition* csParent = fRoot && fRoot->isStructOrClass() ? fRoot : this->csParent();
    if (!csParent) {
        auto csIter = std::find_if(topicParent->fChildren.begin(), topicParent->fChildren.end(),
                [](const Definition* def){ return MarkType::kEnum == def->fMarkType
                || MarkType::kEnumClass == def->fMarkType; } );
        SkASSERT(topicParent->fChildren.end() != csIter);
        csParent = *csIter;
    }
    SkASSERT(csParent);
    this->lfAlways(1);
    if (fPopulators.end() != fPopulators.find(name)) {
        const SubtopicDescriptions& tableDescriptions = this->populator(name);
        this->anchorDef(name, tableDescriptions.fPlural);
        this->lfAlways(1);
        if (tableDescriptions.fDetails.length()) {
            string details = csParent->fName;
            details += " " + tableDescriptions.fDetails;
            this->writeString(details);
            this->lfAlways(1);
        }
    } else {
        this->anchorDef(name, name);
        this->lfAlways(1);
    }
    if (SubtopicKeys::kMembers == name) {
        return; // members output their own table
    }
    const RootDefinition::SubtopicContents& tableContents = fRoot->populator(name.c_str());
    if (SubtopicKeys::kTypedefs == name && fSubtopic && MarkType::kTopic == fSubtopic->fMarkType) {
        topicParent = fSubtopic;
    }
    this->subtopicOut(name, tableContents.fMembers, csParent, topicParent,
            tableContents.fShowClones);
}

void MdOut::subtopicOut(string key, const vector<Definition*>& data, const Definition* csParent,
        const Definition* topicParent, bool showClones) {
    this->writeString(kTableDeclaration);
    this->lfAlways(1);
    this->writeSubtopicTableHeader(key);
    this->lfAlways(1);
    fOddRow = true;
    std::map<string, const Definition*> items;
    for (auto entry : data) {
        if (!BmhParser::IsExemplary(entry)) {
            continue;
        }
        if (entry->csParent() != csParent && entry->topicParent() != topicParent) {
            continue;
        }
        size_t start = entry->fName.find_last_of("::");
        if (MarkType::kConst == entry->fMarkType && entry->fParent
                && MarkType::kEnumClass == entry->fParent->fMarkType
                && string::npos != start && start > 1) {
            start = entry->fName.substr(0, start - 1).rfind("::");
        }
        string entryName = entry->fName.substr(string::npos == start ? 0 : start + 1);
//        fixup_const_function_name(&entry->fName);
        items[entryName] = entry;
    }
    for (auto entry : items) {
        if (entry.second->fDeprecated) {
            continue;
        }
        if (!this->subtopicRowOut(entry.first, entry.second)) {
            return;
        }
        if (showClones && entry.second->fCloned) {
            int cloneNo = 2;
            string builder = entry.second->fName;
            if ("()" == builder.substr(builder.length() - 2)) {
                builder = builder.substr(0, builder.length() - 2);
            }
            builder += '_';
            this->rowOut("overloads", entry.second);
            do {
                string match = builder + to_string(cloneNo);
                auto child = csParent->findClone(match);
                if (!child) {
                    break;
                }
                this->rowOut("", child);
            } while (++cloneNo);
        }
    }
    FPRINTF("</table>");
    this->lf(2);
}

bool MdOut::subtopicRowOut(string keyName, const Definition* entry) {
    const Definition* oneLiner = nullptr;
    for (auto child : entry->fChildren) {
        if (MarkType::kLine == child->fMarkType) {
            oneLiner = child;
            break;
        }
    }
    if (!oneLiner) {
        TextParser parser(entry->fFileName, entry->fStart,
                entry->fContentStart, entry->fLineCount);
        return parser.reportError<bool>("missing #Line");
    }
    TextParser dummy(entry); // for reporting errors, which we won't do
    if (!this->isDefined(dummy, keyName, BmhParser::Resolvable::kOut)) {
        keyName = entry->fName;
        size_t doubleColon = keyName.find("::");
        SkASSERT(string::npos != doubleColon);
        keyName = keyName.substr(doubleColon + 2);
    }
    this->rowOut(keyName.c_str(), string(oneLiner->fContentStart,
            oneLiner->fContentEnd - oneLiner->fContentStart), false);
    return true;
}

void MdOut::writeSubtopicTableHeader(string key) {
    this->htmlOut("<tr>");
    this->htmlOut(kTH_Left);
    if (fPopulators.end() != fPopulators.find(key)) {
        this->writeString(fPopulators[key].fSingular);
    } else {
        this->writeString("Function");
    }
    this->htmlOut("</th>");
    this->lf(1);
    this->htmlOut(kTH_Left);
    this->writeString("Description");
    this->htmlOut("</th>");
    this->htmlOut("</tr>");
}

#undef kTH_Left
