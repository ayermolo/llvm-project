#ifndef LLVM_BINARYFORMAT_ODRTABLE_H
#define LLVM_BINARYFORMAT_ODRTABLE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/StringSaver.h"

// An ODRTable is a table that a frontend can embed in an object file. It
// contains a mapping from names to ODR hashes, which a linker can use to detect
// ODR violations.
//
// The layout of an odrtab is as follows:
// - Header
// - null-terminated Producer string
// - compressed:
//   - array of N ZSymbols
//   - string table
// - array of N Symbols

namespace llvm {
namespace odrtable {
namespace storage {

using Word = support::ulittle32_t;

struct Str {
  support::ulittle32_t Offset;

  StringRef get(StringRef Strtab) const {
    return Strtab.data() + Offset;
  }
};

template <typename T> struct Range {
  support::ulittle32_t Offset, Size;

  ArrayRef<T> get(StringRef Symtab) const {
    return {reinterpret_cast<const T *>(Symtab.data() + Offset), Size};
  }
};

struct ZSymbol {
  Str Name, File;
  support::ulittle32_t Line;
};

struct Symbol {
  support::ulittle64_t NameHash;
  support::ulittle32_t ODRHash;
};

struct Header {
  support::ulittle32_t Version;
  support::ulittle32_t SymbolOffset, NumSymbols;
  support::ulittle32_t ZODRTabSize;

  StringRef getProducer() const {
    return reinterpret_cast<const char *>(this + 1);
  }
  ArrayRef<Symbol> getSymbols() const {
    return {reinterpret_cast<const Symbol *>(
                reinterpret_cast<const char *>(this) + SymbolOffset),
            NumSymbols};
  }
  size_t size() const { return SymbolOffset + NumSymbols * sizeof(Symbol); }
};

}

class Builder {
  StringTableBuilder StrtabBuilder{StringTableBuilder::ELF};

  BumpPtrAllocator Alloc;
  StringSaver Saver{Alloc};
  std::vector<storage::Symbol> Syms;
  std::vector<storage::ZSymbol> ZSyms;

  void setStr(storage::Str &S, StringRef Value) {
    S.Offset = StrtabBuilder.add(Saver.save(Value));
  }

public:
  void add(StringRef Name, StringRef File, unsigned Line, uint32_t ODRHash);
  Error build(StringRef Producer, SmallVector<char, 0> &ODRTab);
};

struct Diag {
  std::string Name;
  struct Def {
    void *Source;
    std::string File;
    unsigned Line;
  };
  std::vector<Def> Defs;
};

struct InputFile {
  void *Source;
  ArrayRef<uint8_t> ODRTable;
};

Expected<std::vector<Diag>> check(ArrayRef<InputFile> Inputs);

}
}

#endif
