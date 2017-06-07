#include "llvm/BinaryFormat/ODRTable.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/xxhash.h"
#include <set>

using namespace llvm;
using namespace odrtable;

enum { kCurrentVersion = 0 };

void Builder::add(StringRef Name, StringRef File, unsigned Line, uint32_t ODRHash) {
  storage::Symbol Sym;
  Sym.NameHash = xxHash64(Name);
  Sym.ODRHash = ODRHash;
  Syms.push_back(Sym);

  storage::ZSymbol ZSym;
  setStr(ZSym.Name, Name);
  setStr(ZSym.File, File);
  ZSym.Line = Line;
  ZSyms.push_back(ZSym);
}

Error Builder::build(StringRef Producer, SmallVector<char, 0> &ODRTab) {
  storage::Header Hdr;
  Hdr.Version = kCurrentVersion;
  ODRTab.resize(sizeof(Hdr) + Producer.size() + 1);
  memcpy(ODRTab.data() + sizeof(Hdr), Producer.data(), Producer.size());

  SmallVector<char, 0> ZODRTab;
  ZODRTab.insert(ZODRTab.end(), reinterpret_cast<const char *>(ZSyms.data()),
                 reinterpret_cast<const char *>(ZSyms.data() + ZSyms.size()));
  raw_svector_ostream OS(ZODRTab);
  StrtabBuilder.finalizeInOrder();
  StrtabBuilder.write(OS);
  if (Error Err = zlib::compress({ZODRTab.data(), ZODRTab.size()}, ODRTab))
    return Err;

  Hdr.SymbolOffset = ODRTab.size();
  Hdr.NumSymbols = Syms.size();
  Hdr.ZODRTabSize = ZODRTab.size();

  ODRTab.insert(ODRTab.end(), reinterpret_cast<const char *>(Syms.data()),
                reinterpret_cast<const char *>(Syms.data() + Syms.size()));

  *reinterpret_cast<storage::Header *>(ODRTab.data()) = Hdr;
  return Error::success();
}

Expected<std::vector<Diag>> odrtable::check(ArrayRef<InputFile> Inputs) {
  std::vector<Diag> Diags;
  std::string Producer;

  struct ParsedInput {
    void *Source;
    StringRef ZODRTab, CompressedZODRTab;
    size_t ZODRTabSize;
    ArrayRef<storage::Symbol> Symbols;
  };
  std::vector<ParsedInput> ParsedInputs;
  BumpPtrAllocator ZODRTabAlloc;

  struct HashSymbol {
    uint32_t ODRHash;
    size_t InputIndex, SymIndex;
  };
  DenseMap<uint64_t, HashSymbol> HashSymtab;

  struct Symbol {
    std::set<uint64_t> ODRHash;
    Diag::Def DiagDef;
    unsigned DiagIndex;
  };
  StringMap<Symbol> Symtab;

  auto AddToSymtab = [&](ParsedInput &Input, size_t SymIndex) -> Error {
    if (Input.ZODRTab.empty()) {
      char *ZODRTab =
          static_cast<char *>(ZODRTabAlloc.Allocate(Input.ZODRTabSize, 1));
      size_t Size = Input.ZODRTabSize;
      if (Error Err = zlib::uncompress(Input.CompressedZODRTab, ZODRTab, Size))
        return Err;
      Input.ZODRTab = {ZODRTab, Size};
    }

    StringRef Strtab =
        Input.ZODRTab.substr(Input.Symbols.size() * sizeof(storage::ZSymbol));

    const storage::Symbol &SSym = Input.Symbols[SymIndex];
    auto &ZSym = reinterpret_cast<const storage::ZSymbol *>(
        Input.ZODRTab.data())[SymIndex];

    StringRef Name = ZSym.Name.get(Strtab);
    Symbol InsSym = {
        {SSym.ODRHash}, {Input.Source, ZSym.File.get(Strtab), ZSym.Line}, -1u};
    auto &Sym = Symtab.insert({Name, InsSym}).first->second;
    if (Sym.ODRHash.insert(SSym.ODRHash).second) {
      Diag *D;
      if (Sym.DiagIndex == -1u) {
        Sym.DiagIndex = Diags.size();
        Diags.emplace_back();
        D = &Diags.back();
        D->Name = Name;
        D->Defs.push_back(Sym.DiagDef);
      } else {
        D = &Diags[Sym.DiagIndex];
      }

      D->Defs.push_back(InsSym.DiagDef);
    }

    return Error::success();
  };

  StringMap<size_t> Diagtab;

  for (const InputFile &I : Inputs) {
    StringRef ODRTable = {reinterpret_cast<const char *>(I.ODRTable.data()),
                          I.ODRTable.size()};
    while (ODRTable.size() >= sizeof(storage::Header)) {
      auto &Hdr = *reinterpret_cast<const storage::Header *>(ODRTable.data());
      StringRef HdrProducer = Hdr.getProducer();

      if (Hdr.Version != kCurrentVersion)
        return Diags;
      if (Producer.empty())
        HdrProducer = Hdr.getProducer();
      else if (HdrProducer != Hdr.getProducer())
        // Don't bother checking odr for more than one producer at once.
        return Diags;

      size_t ZODRTabOffset = sizeof(storage::Header) + HdrProducer.size() + 1;
      StringRef ZODRTab = {ODRTable.data() + ZODRTabOffset,
                           Hdr.SymbolOffset - ZODRTabOffset};
      ArrayRef<storage::Symbol> Syms = Hdr.getSymbols();
      ParsedInputs.push_back({I.Source, {}, ZODRTab, Hdr.ZODRTabSize, Syms});

      for (unsigned I = 0; I != Syms.size(); ++I) {
        const storage::Symbol &Sym = Syms[I];

        HashSymbol InsHashSym = {Sym.ODRHash, ParsedInputs.size() - 1, I};
        auto &HashSym =
            HashSymtab.insert({Sym.NameHash, InsHashSym}).first->second;
        if (HashSym.ODRHash != Sym.ODRHash) {
          HashSym.ODRHash = Sym.ODRHash;
          if (HashSym.InputIndex != -1u) {
            if (Error Err = AddToSymtab(ParsedInputs[HashSym.InputIndex],
                                        HashSym.SymIndex))
              return std::move(Err);
            HashSym.InputIndex = -1u;
          }
          if (Error Err = AddToSymtab(ParsedInputs.back(), I))
            return std::move(Err);
        }
      }

      // Move to the next odr table.
      ODRTable = ODRTable.substr(Hdr.size());
    }
  }

  return Diags;
}
