/// \file ROOT/RNTupleView.hxx
/// \ingroup NTuple ROOT7
/// \author Jakob Blomer <jblomer@cern.ch>
/// \date 2018-10-05
/// \warning This is part of the ROOT 7 prototype! It will change without notice. It might trigger earthquakes. Feedback
/// is welcome!

/*************************************************************************
 * Copyright (C) 1995-2019, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT7_RNTupleView
#define ROOT7_RNTupleView

#include <ROOT/RField.hxx>
#include <ROOT/RNTupleUtil.hxx>
#include <string_view>

#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>
#include <unordered_map>

namespace ROOT {
namespace Experimental {


// clang-format off
/**
\class ROOT::Experimental::RNTupleGlobalRange
\ingroup NTuple
\brief Used to loop over indexes (entries or collections) between start and end
*/
// clang-format on
class RNTupleGlobalRange {
private:
   const NTupleSize_t fStart;
   const NTupleSize_t fEnd;
public:
   class RIterator {
   private:
      NTupleSize_t fIndex = kInvalidNTupleIndex;
   public:
      using iterator = RIterator;
      using iterator_category = std::forward_iterator_tag;
      using value_type = NTupleSize_t;
      using difference_type = NTupleSize_t;
      using pointer = NTupleSize_t*;
      using reference = NTupleSize_t&;

      RIterator() = default;
      explicit RIterator(NTupleSize_t index) : fIndex(index) {}
      ~RIterator() = default;

      iterator  operator++(int) /* postfix */        { auto r = *this; fIndex++; return r; }
      iterator& operator++()    /* prefix */         { ++fIndex; return *this; }
      reference operator* ()                         { return fIndex; }
      pointer   operator->()                         { return &fIndex; }
      bool      operator==(const iterator& rh) const { return fIndex == rh.fIndex; }
      bool      operator!=(const iterator& rh) const { return fIndex != rh.fIndex; }
   };

   RNTupleGlobalRange(NTupleSize_t start, NTupleSize_t end) : fStart(start), fEnd(end) {}
   RIterator begin() { return RIterator(fStart); }
   RIterator end() { return RIterator(fEnd); }
};


// clang-format off
/**
\class ROOT::Experimental::RNTupleClusterRange
\ingroup NTuple
\brief Used to loop over entries of collections in a single cluster
*/
// clang-format on
class RNTupleClusterRange {
private:
   const DescriptorId_t fClusterId;
   const ClusterSize_t::ValueType fStart;
   const ClusterSize_t::ValueType fEnd;
public:
   class RIterator {
   private:
      RClusterIndex fIndex;
   public:
      using iterator = RIterator;
      using iterator_category = std::forward_iterator_tag;
      using value_type = RClusterIndex;
      using difference_type = RClusterIndex;
      using pointer = RClusterIndex*;
      using reference = RClusterIndex&;

      RIterator() = default;
      explicit RIterator(RClusterIndex index) : fIndex(index) {}
      ~RIterator() = default;

      iterator  operator++(int) /* postfix */        { auto r = *this; fIndex++; return r; }
      iterator& operator++()    /* prefix */         { fIndex++; return *this; }
      reference operator* ()                         { return fIndex; }
      pointer   operator->()                         { return &fIndex; }
      bool      operator==(const iterator& rh) const { return fIndex == rh.fIndex; }
      bool      operator!=(const iterator& rh) const { return fIndex != rh.fIndex; }
   };

   RNTupleClusterRange(DescriptorId_t clusterId, ClusterSize_t::ValueType start, ClusterSize_t::ValueType end)
      : fClusterId(clusterId), fStart(start), fEnd(end) {}
   RIterator begin() { return RIterator(RClusterIndex(fClusterId, fStart)); }
   RIterator end() { return RIterator(RClusterIndex(fClusterId, fEnd)); }
};


namespace Internal {
// TODO(bgruber): convert this trait into a requires clause in C++20
template <typename FieldT, typename SFINAE = void>
inline constexpr bool isMappable = false;

template <typename FieldT>
inline constexpr bool isMappable<FieldT, std::void_t<decltype(std::declval<FieldT>().Map(NTupleSize_t{}))>> = true;
} // namespace Internal


// clang-format off
/**
\class ROOT::Experimental::RNTupleView
\ingroup NTuple
\brief An RNTupleView provides read-only access to a single field of the ntuple

The view owns a field and its underlying columns in order to fill an ntuple value object with data. Data can be
accessed by index. For top-level fields, the index refers to the entry number. Fields that are part of
nested collections have global index numbers that are derived from their parent indexes.

Fields of simple types with a Map() method will use that and thus expose zero-copy access.
*/
// clang-format on
template <typename T>
class RNTupleView {
   friend class RNTupleReader;
   friend class RNTupleCollectionView;

   using FieldT = RField<T>;

private:
   /// fFieldId has fParent always set to null; views access nested fields without looking at the parent
   FieldT fField;
   /// Used as a Read() destination for fields that are not mappable
   RFieldBase::RValue fValue;

   RNTupleView(DescriptorId_t fieldId, Detail::RPageSource *pageSource)
      : fField(pageSource->GetSharedDescriptorGuard()->GetFieldDescriptor(fieldId).GetFieldName()),
        fValue(fField.CreateValue())
   {
      fField.SetOnDiskId(fieldId);
      Internal::CallConnectPageSourceOnField(fField, *pageSource);
      if ((fField.GetTraits() & RFieldBase::kTraitMappable) && fField.HasReadCallbacks())
         throw RException(R__FAIL("view disallowed on field with mappable type and read callback"));
   }

public:
   RNTupleView(const RNTupleView& other) = delete;
   RNTupleView(RNTupleView&& other) = default;
   RNTupleView& operator=(const RNTupleView& other) = delete;
   RNTupleView& operator=(RNTupleView&& other) = default;
   ~RNTupleView() = default;

   const FieldT &GetField() const { return fField; }
   RNTupleGlobalRange GetFieldRange() const { return RNTupleGlobalRange(0, fField.GetNElements()); }

   const T &operator()(NTupleSize_t globalIndex)
   {
      if constexpr (Internal::isMappable<FieldT>)
         return *fField.Map(globalIndex);
      else {
         fValue.Read(globalIndex);
         return fValue.GetRef<T>();
      }
   }

   const T &operator()(RClusterIndex clusterIndex)
   {
      if constexpr (Internal::isMappable<FieldT>)
         return *fField.Map(clusterIndex);
      else {
         fValue.Read(clusterIndex);
         return fValue.GetRef<T>();
      }
   }

   // TODO(bgruber): turn enable_if into requires clause with C++20
   template <typename C = T, std::enable_if_t<Internal::isMappable<FieldT>, C*> = nullptr>
   const C *MapV(NTupleSize_t globalIndex, NTupleSize_t &nItems)
   {
      return fField.MapV(globalIndex, nItems);
   }

   // TODO(bgruber): turn enable_if into requires clause with C++20
   template <typename C = T, std::enable_if_t<Internal::isMappable<FieldT>, C *> = nullptr>
   const C *MapV(RClusterIndex clusterIndex, NTupleSize_t &nItems)
   {
      return fField.MapV(clusterIndex, nItems);
   }
};

// clang-format off
/**
\class ROOT::Experimental::RNTupleView<void>
\ingroup NTuple
\brief An RNTupleView where the type is not known at compile time.

Can be used to read individual fields whose type is unknown. The void view gives access to the RValue
in addition to the field, so that the read object can be retrieved.
*/
// clang-format on
template <>
class RNTupleView<void> {
   friend class RNTupleReader;
   friend class RNTupleCollectionView;

private:
   std::unique_ptr<RFieldBase> fField;
   RFieldBase::RValue fValue;

   static std::unique_ptr<RFieldBase> CreateField(DescriptorId_t fieldId, const RNTupleDescriptor &desc)
   {
      return desc.GetFieldDescriptor(fieldId).CreateField(desc);
   }

   RNTupleView(DescriptorId_t fieldId, Detail::RPageSource *pageSource)
      : fField(CreateField(fieldId, pageSource->GetSharedDescriptorGuard().GetRef())), fValue(fField->CreateValue())
   {
      fField->SetOnDiskId(fieldId);
      Internal::CallConnectPageSourceOnField(*fField, *pageSource);
   }

public:
   RNTupleView(const RNTupleView &other) = delete;
   RNTupleView(RNTupleView &&other) = default;
   RNTupleView &operator=(const RNTupleView &other) = delete;
   RNTupleView &operator=(RNTupleView &&other) = default;
   ~RNTupleView() = default;

   const RFieldBase &GetField() const { return *fField; }
   const RFieldBase::RValue &GetValue() const { return fValue; }
   RNTupleGlobalRange GetFieldRange() const { return RNTupleGlobalRange(0, fField->GetNElements()); }

   void operator()(NTupleSize_t globalIndex) { fValue.Read(globalIndex); }
   void operator()(RClusterIndex clusterIndex) { fValue.Read(clusterIndex); }
};

// clang-format off
/**
\class ROOT::Experimental::RNTupleCollectionView
\ingroup NTuple
\brief A view for a collection, that can itself generate new ntuple views for its nested fields.
*/
// clang-format on
class RNTupleCollectionView : public RNTupleView<ClusterSize_t> {
   friend class RNTupleReader;

private:
   Detail::RPageSource* fSource;
   DescriptorId_t fCollectionFieldId;

   RNTupleCollectionView(DescriptorId_t fieldId, Detail::RPageSource *source)
      : RNTupleView<ClusterSize_t>(fieldId, source), fSource(source), fCollectionFieldId(fieldId)
   {}

public:
   RNTupleCollectionView(const RNTupleCollectionView &other) = delete;
   RNTupleCollectionView(RNTupleCollectionView &&other) = default;
   RNTupleCollectionView &operator=(const RNTupleCollectionView &other) = delete;
   RNTupleCollectionView &operator=(RNTupleCollectionView &&other) = default;
   ~RNTupleCollectionView() = default;

   RNTupleClusterRange GetCollectionRange(NTupleSize_t globalIndex) {
      ClusterSize_t size;
      RClusterIndex collectionStart;
      fField.GetCollectionInfo(globalIndex, &collectionStart, &size);
      return RNTupleClusterRange(collectionStart.GetClusterId(), collectionStart.GetIndex(),
                                 collectionStart.GetIndex() + size);
   }
   RNTupleClusterRange GetCollectionRange(RClusterIndex clusterIndex)
   {
      ClusterSize_t size;
      RClusterIndex collectionStart;
      fField.GetCollectionInfo(clusterIndex, &collectionStart, &size);
      return RNTupleClusterRange(collectionStart.GetClusterId(), collectionStart.GetIndex(),
                                 collectionStart.GetIndex() + size);
   }

   /// Raises an exception if there is no field with the given name.
   template <typename T>
   RNTupleView<T> GetView(std::string_view fieldName) {
      auto fieldId = fSource->GetSharedDescriptorGuard()->FindFieldId(fieldName, fCollectionFieldId);
      if (fieldId == kInvalidDescriptorId) {
         throw RException(R__FAIL("no field named '" + std::string(fieldName) + "' in RNTuple '" +
                                  fSource->GetSharedDescriptorGuard()->GetName() + "'"));
      }
      return RNTupleView<T>(fieldId, fSource);
   }
   /// Raises an exception if there is no field with the given name.
   RNTupleCollectionView GetCollectionView(std::string_view fieldName)
   {
      auto fieldId = fSource->GetSharedDescriptorGuard()->FindFieldId(fieldName, fCollectionFieldId);
      if (fieldId == kInvalidDescriptorId) {
         throw RException(R__FAIL("no field named '" + std::string(fieldName) + "' in RNTuple '" +
                                  fSource->GetSharedDescriptorGuard()->GetName() + "'"));
      }
      return RNTupleCollectionView(fieldId, fSource);
   }

   ClusterSize_t operator()(NTupleSize_t globalIndex) {
      ClusterSize_t size;
      RClusterIndex collectionStart;
      fField.GetCollectionInfo(globalIndex, &collectionStart, &size);
      return size;
   }
   ClusterSize_t operator()(RClusterIndex clusterIndex)
   {
      ClusterSize_t size;
      RClusterIndex collectionStart;
      fField.GetCollectionInfo(clusterIndex, &collectionStart, &size);
      return size;
   }
};

} // namespace Experimental
} // namespace ROOT

#endif
