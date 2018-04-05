// Copyright (C) 2012 von Karman Institute for Fluid Dynamics, Belgium
//
// This software is distributed under the terms of the
// GNU Lesser General Public License version 3 (LGPLv3).
// See doc/lgpl.txt and doc/gpl.txt for the license text.

/// IDEA:
///      - Lazy deletion of ghost points (wait until sync)
///      - Maybe change alter map -> hashmap?
///      - For shared memory implementation
///        need to check writes to ghost points? (overload operator)
/// TO REMEMBER:
///      - Adding/Removing  a point should be an easy operationche
///        (no collective com)
///      - Synchronisation cost should be abs. minimum
///  - Per-element overhead should be low
/// TODO: - Sync speed??
///       - Make helper class for sync; Allow for easy replace of
///         sync method
///       - Add collection of statistics (with configure option?)
///         Number of bytes sent/received, number of syncs, ...
///       - Data and Metadata are seperated for a reason
///         (in fact there are multiple reasons)
///       - Remove _NOT_FOUND -> change to exception
/// Changes:
///       - 30/04/2004: * Switch to new MPI Datatype framework
///       - 02/05/2004: * Add size parameter
///                       (needed because of failure to have a compile-time
///                        size of the state vectors)
///       - 06/10/2005: * Added support for continuous global index
///                     * Cleanup: removed old unsafe constructs
///                       and added more safety (foolproof) checks
///                     * Removed "using ..." (not allowed in headers)
/// @todo make assert inline function that checks and throws if neccesairy

#ifndef COOLFluiD_Common_MPICommPattern_hh
#define COOLFluiD_Common_MPICommPattern_hh

#include <set>
#include <sstream>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>

#include "Common/COOLFluiD.hh"
#include "Common/PE.hh"
#include "Common/ArrayAllocator.hh"
#include "Common/CFLog.hh"
#include "Common/SharedPtr.hh"
#include "Common/CFPrintContainer.hh"
#include "Common/CFMultiMap.hh"
#include "Common/MPI/ParVectorException.hh"
#include "Common/MPI/MPIException.hh"
#include "Common/MPI/MPIHelper.hh"
#include "Common/MPI/MPIStructDef.hh"

//////////////////////////////////////////////////////////////////////////////

namespace COOLFluiD {
    namespace Common {

//////////////////////////////////////////////////////////////////////////////

#ifdef CF_ENABLE_PARALLEL_DEBUG
void WriteCommPatternHelper (MPI_Comm Comm,
  CFuint LocalSize, CFuint GhostSize,
  std::vector<CFuint> & Sends,
  std::vector<CFuint> & Receives)
{
  // In order not to overwrite files from another parvector
  static CFuint Count = 0;
  
  CFAUTOTRACE;
  
  CFLogNotice ("Writing communication pattern...\n");
  
  int CommRank, CommSize;
  
  Common::CheckMPIStatus(MPI_Comm_size (Comm, &CommSize));
  Common::CheckMPIStatus(MPI_Comm_rank (Comm, &CommRank));
  
  std::ostringstream S;
  S << "parvector_pattern." << Count << ".dot";
  
  std::string Name = S.str();
  S.str(std::string ());
  
  MPI_File FileHandle;
  
  Common::CheckMPIStatus(MPI_File_open (Comm, const_cast<char *>(Name.c_str()),
					MPI_MODE_SEQUENTIAL|MPI_MODE_CREATE|MPI_MODE_WRONLY,
					MPI_INFO_NULL, &FileHandle));
  
  MPI_Barrier (Comm);
  
  Common::CheckMPIStatus(MPI_File_set_size (FileHandle, 0));
  
  MPI_Barrier (Comm);
  
  if (!CommRank)
    {
      S << "digraph parvector {\n";
    }
  
  S << "  P" << CommRank << " [label=\"CPU" << CommRank << " ("
    << LocalSize << "+" << GhostSize << ")\"];\n";
  for (CFuint i=0; i<static_cast<CFuint>(CommSize); ++i)
    {
      if (!Sends[i])
      continue;
      S << "  P" << CommRank << " -> P" << i << " [label=\""
	<< Sends[i] << "\"];\n";
    }
  
  std::string Buf = S.str();
  Common::CheckMPIStatus(MPI_File_write_ordered (FileHandle,
						 const_cast<char *>(Buf.c_str()),
						 Buf.size(),
						 MPI_CHAR, MPI_STATUS_IGNORE));
  S.str(std::string());
  
  
  if (!CommRank)
    {
      S << "}\n";
    }
  
  Buf = S.str();
  Common::CheckMPIStatus(MPI_File_write_ordered (FileHandle,
						 const_cast<char *> (Buf.c_str()),
						 Buf.size(),
						 MPI_CHAR, MPI_STATUS_IGNORE));
  S.str(std::string());
  
  
  Common::CheckMPIStatus(MPI_File_close (&FileHandle));
  
  ++Count;
}
#endif


/// Template class
///***************************************************************************/
template < typename DATA >
class MPICommPattern
{
public:
  //===============================================================
  //======================== PUBLIC types =========================
  //===============================================================

  /// Represents the type stored in the vector
  typedef typename DATA::TYPE T;

  /// The index type (inherited from ArrayAllocator)
  typedef typename COOLFluiD::Common::ArrayAllocator<T>::IndexType  IndexType;

  /// Represents the type stored in the vector
  typedef T _ElementType;

private:

  //=========================================================
  //============== Private structs & typedefs ===============
  //=========================================================

  struct IdxStruct
  {
    // GlobalIndex is also used for the free list
    IndexType GlobalIndex;
  };
  typedef IdxStruct DataType;

  typedef std::map<IndexType,IndexType> TGhostMap;
  typedef std::map<IndexType,IndexType> TIndexMap;


  //============================================================
  //============= private variables ============================
  //============================================================

  /// Contains the indexes
  std::vector<std::vector<IndexType> > _GhostSendList;
  std::vector<std::vector<IndexType> > _GhostReceiveList;

  /// The real size of an element stored in the vector
  /// (as opposed to the size of the element TYPE)
  int _ElementSize;
  
  /// The number of local owned points 
  int _LocalSize;
  
  /// The number of Ghost points
  int _GhostSize;
  
  /// The index of the next free element in the vector
  /// (can be NO_MORE_FREE)
  IndexType _NextFree;

  /// This stores a pointer to the actual element data
  DATA* m_data;

  /// This stores the metadata for an element
  COOLFluiD::Common::ArrayAllocator<DataType> _MetaData;

  /// Do we have an index?
  bool    _IsIndexed;

  /// Some constants... They are the same for every
  /// instantiation; TODO: Try out some scheme with extern
  /// to avoid duplication of useless constants
  static const IndexType _NO_MORE_FREE;
  static const IndexType _FLAG_DELETED;
  static const IndexType _FLAG_GHOST;
  static const IndexType _NOT_FOUND;

  /// The used tags
  /// TODO: Check out interference from multiple vectors
  /// Solution1:
  ///    * Limit to at most one par_vector in each communicator
  /// Solution2:
  ///    * Have it request a free tag from some allocator
  ///      (WARNING! CONSISTENCY IN PARALLEL RUN!)
  static const int _MPI_TAG_BUILDGHOSTMAP;
  static const int _MPI_TAG_SYNC;

  /// The rank of this CPU (cached for speed reasons)
  int _CommRank;

  /// The size of the communicator (cached for speed reasons)
  int _CommSize;

  /// Check to see if InitMPI was called
  bool _InitMPIOK;

  /// Is the CGlobalMap is valid
  bool _CGlobalValid;

  /// The used Communicator
  MPI_Comm _Communicator;
  
  /// mapping from global ghost IDs to donor ranks
  Common::SharedPtr<Common::CFMultiMap<CFuint, CFuint> > m_mapGhost2Donor;
  
  /// send counts for ghost points
  std::vector<int> m_sendCount;

  /// receive counts for ghost points
  std::vector<int> m_recvCount;
  
  /// send displacements for ghost points
  std::vector<int> m_sendDispl;
    
  /// recv displacements for ghost points
  std::vector<int> m_recvDispl;

  /// send local IDs
  std::vector<CFuint> m_sendLocalIDs;
    
  /// recv local IDs
  std::vector<CFuint> m_recvLocalIDs;
  
  /// send buffer
  std::vector<T> m_sendBuf;
    
  /// recv local IDs
  std::vector<T> m_recvBuf;
  
  /// The Index for ghost points
  TGhostMap _GhostMap;

  /// The index for local points
  TIndexMap _IndexMap;

  /// Array containing for each rank the type to send
  /// (can be in seperate subclass)
  std::vector<MPI_Datatype> _SendTypes;

  /// Array containing for each rank the type to receive
  /// (can be in seperate subclass)
  std::vector<MPI_Datatype> _ReceiveTypes;

  /// This is the MPI type of 1 element
  MPI_Datatype _BasicType;

  /// To track the requests
  std::vector<MPI_Request> _ReceiveRequests;
  std::vector<MPI_Request> _SendRequests;

  /// Data of the CGLobal map
  std::vector<IndexType> _CGlobal;

  /// The CGlobal index of our first local element
  IndexType _FirstCGlobal;

  //=============================================================
  //=================== Private Functions =======================
  //=============================================================

  /// Allocate a free element and return the index
  IndexType AllocNext ();

  /// Enlarge the capacity of the vector by growBy
  /// If growBy is 0, select the optimal enlargment
  void grow (IndexType growBy = 0);

  /// Remove overcapacity
  void Shrink ();

  /// Ghost map builder help routines
  void Sync_BroadcastNeeded ();
  void Sync_BuildSendTypes ();
  void Sync_BuildReceiveList ();
  void Sync_BuildReceiveTypes ();

  void Sync_BuildTypeHelper (const std::vector<std::vector<IndexType> > & V,
                                   std::vector<MPI_Datatype> & MPIType ) const;

  /// Find functions (for internal use)
  /// These take advantage of a index map if one is present
  IndexType FindLocal (IndexType GlobalIndex) const;
  IndexType FindGhost (IndexType GlobalIndex) const;

  void AddLocalIndex (IndexType LocalIndex, IndexType GlobalIndex);
  void AddGhostIndex (IndexType LocalIndex, IndexType GlobalIndex);

  IndexType NormalIndex (IndexType GlobalIndex) const;

  //= Flag helper functions
  inline bool IsFlagSet (IndexType GlobalIndex, IndexType Fl) const;
  inline IndexType SetFlag (IndexType GlobalIndex, IndexType Fl) const;
  inline IndexType ClearFlag (IndexType GLobalIndex, IndexType Fl) const;

#ifdef CF_ENABLE_PARALLEL_DEBUG
  void WriteCommPattern () const;
#endif

public: // funcions

  /// Constructor.
  /// WARNING: Size parameter is IGNORED!
  MPICommPattern (const std::string nspaceName, DATA* data, 
		  const T& Init, CFuint Size, CFuint ESize = 0);
  
  /// Destructor
  /// Before destructing the class, DoneMPI should be called
  ~MPICommPattern ();

  /// Returns the list of ghost nodes (by processor rank) to be sent to
  /// another processor
  const std::vector< std::vector< IndexType > >& GetGhostSendList() const
  {
    return _GhostSendList;
  }

  /// Returns the list of ghost nodes (by processor rank) to be received
  /// from another processor
  const std::vector< std::vector< IndexType > >& GetGhostReceiveList() const
  {
    return _GhostReceiveList;
  }

  /// Return the total vector size (not counting ghost points)
  /// This is a COLLECTIVE operation!
  IndexType GetGlobalSize () const; /* collective */

  /// Return the number of local (non-ghost) points
  /// Local operation.
  IndexType GetLocalSize () const; /* local */

  /// return the number of ghostpoints
  /// Local operation.
  IndexType GetGhostSize () const; /* local */

  /// Insert a new ghost point
  /// Local operation.
  /// For now, NO Add operations are allowed after
  /// BuildGhostMap is called.
  IndexType AddGhostPoint (IndexType GlobalIndex);

  /// Insert new local point
  /// Local operation.
  /// For now, NO add operations are allowed after
  /// BuildGhostMap is called.
  IndexType AddLocalPoint (IndexType GlobalIndex);

  /// Local to global mapping:
  ///  (LOCAL operation)
  /// To determine if an element is a ghost element,
  /// use IsGhost ()
  IndexType LocalToGlobal (IndexType LocalIndex) const; /* local */

  /// Global to local mapping.
  /// Can be slow if no indexes were built.
  IndexType GlobalToLocal (IndexType GlobalIndex) const; /* local */

  /// Start the synchronisation
  /// Collective operation.
  /// Before this can be called, InitMPI had to be called.
  void BeginSync ();

  /// Wait for the end of the synchronisation
  /// Collective.
  void EndSync ();

  /// Synchronize the ghost entries (collective) with corresponding updatable values
  void synchronize();
  
  /// Build internal data structures
  /// (to be called after adding ghost points but before doing a sync)
  /// Collective.
  /// @pre InitMPI needs to be called before this.
  void BuildGhostMap(const std::string& algo) 
  {
    cf_assert(algo == "Old" || algo == "Bcast" || algo == "AllToAll");
    if (algo == "Old") {
      BuildGhostMapOld(); 
    }
    else if (_CommSize > 1) {
      if (algo == "Bcast") BuildGhostMapBcast();
      if (algo == "AllToAll") BuildGhostMapAllToAll();
    }
  }  
  
  /// Build the ghost mapping for synchronization with the new algorithm 
  /// based on MPI_Alltoall and MPI_Alltoallv
  /// @author Andrea Lani
  void BuildGhostMapAllToAll();
  
  /// Build the ghost mapping for synchronization with the new algorithm 
  /// based on MPI_Bcast and MPI_Alltoall
  /// @author Andrea Lani
  void BuildGhostMapBcast();
  
  /// Build the ghost mapping for synchronization with the old algorithm
  void BuildGhostMapOld();
   
  /// Set the mapping from global ghost IDs to donor ranks
  void setMapGhost2DonorRanks
  (Common::SharedPtr<Common::CFMultiMap<CFuint, CFuint> >& mapGhost2Donor) 
  {
    m_mapGhost2Donor.reset(mapGhost2Donor);
  }
  
  /// Create the indexes
  /// (to speed up index operations)
  /// (An index counter is maintained)
  void CreateIndex();

  /// Free the indexes (to conserve memory)
  void DestroyIndex();

  /// Given a pointer, try to find the index of the element
  /// (HACK ! DON'T USE)
  IndexType PointerToIndex (const void * Ptr) const;

  /// Write some internal state information
  void DumpInternalData ();

  /// print local+global index to a dumpfile
  void DumpContents ();
  
  /// Initialize MPI functions
  void InitMPI (const std::string nspaceName);
  
  /// Free MPI resources
  void DoneMPI ();

  /// Make sure we can have up to capacity elements
  /// before needing to allocate
  /// (and possible invalidate pointers & references)
  void reserve (IndexType capacity, CFuint elementSize,
		const std::string nspaceName);

  /// Returns true if the given local index is a ghost element
  inline bool IsGhost (IndexType LocalID) const;

  //=========================================================================
  //===== Global Continuous Indexes =========================================
  //=========================================================================

  /// Build a continuous global mapping.
  /// Should be called after all points are added,
  /// and after BuildMap is called
  /// This function uses indexes if available
  void BuildCGlobal ();

  /// returns true if a global continuous mapping is available
  bool HasCGlobal () const;

  /// free the continuous index
  void FreeCGlobal ();

  /// Lookup the global continuous ID of a local element
  inline IndexType LocalToCGlobal (IndexType LocalID) const;
  
  /// Return the local size: This is the number of
  /// locally owned points incremented by the number of ghost points
  /// Local operation.
  size_t size() const {return GetLocalSize() + GetGhostSize();}
    
private:
  
  /// (Used by internal functions)
  /// This function is used to inform that the CGlobal map is now
  /// invalid because of changes to the local vector size.
  void InvalidateCGlobal ();
  
  /// Build the CGlobal map for the local elements
  void BuildCGlobalLocal (std::vector<IndexType> & Ghosts);
};

///  Static constants
///*************************************************************************/

//
// MPI TAGS
//
template <typename DATA>
const int MPICommPattern<DATA>::_MPI_TAG_BUILDGHOSTMAP  = 100;
      
template <typename DATA>
const int MPICommPattern<DATA>::_MPI_TAG_SYNC  =
  MPICommPattern<DATA>::_MPI_TAG_BUILDGHOSTMAP + 1;

//////////////////////////////////////////////////////////////////////////////

//
// Index flags
//
template <typename DATA>
const typename MPICommPattern<DATA>::IndexType MPICommPattern<DATA>::_NO_MORE_FREE =
  std::numeric_limits<typename MPICommPattern<DATA>::IndexType>::max();
      
template <typename DATA>
const typename MPICommPattern<DATA>::IndexType MPICommPattern<DATA>::_FLAG_DELETED =
  (std::numeric_limits<IndexType>::max()/ 2) + 1;
      
template <typename DATA>
const typename MPICommPattern<DATA>::IndexType MPICommPattern<DATA>::_FLAG_GHOST =
  (MPICommPattern<DATA>::_FLAG_DELETED >> 1);
      
template <typename DATA>
const typename MPICommPattern<DATA>::IndexType MPICommPattern<DATA>::_NOT_FOUND =
	   std::numeric_limits<typename MPICommPattern<DATA>::IndexType>::max();

/*============================================================================
  == CGlobal Functions ========================================================
  =============================================================================*/

template <typename DATA>
void MPICommPattern<DATA>::BuildCGlobalLocal (std::vector<IndexType> & Ghosts)
{
  // Ghosts contains the local index of the ghost element
  // The number of locally owned elements
  const CFuint LocalOwned = GetLocalSize();
  CFuint StartID = 0;

  // Prefix scan
  MPI_Scan (const_cast<CFuint*>(&LocalOwned), 
	    &StartID, 1, MPIStructDef::getMPIType(&StartID),
	    MPI_SUM, _Communicator);
  
  // Correct for exclusive scan
  StartID -= LocalOwned;
  
  _FirstCGlobal = StartID;
  CFLog (DEBUG_MIN, "First CGlobal ID: " << _FirstCGlobal << ", Count=" << LocalOwned << "\n");
  
  // resize CGlobal
  _CGlobal.resize (GetLocalSize()+GetGhostSize());
  
  const size_t gsize = size();
  CFLog (DEBUG_MIN, "MPICommPattern<DATA>::reserve() -> m_data->GetTotalSize() = " << gsize << "\n");
  for (CFuint i=0; i< gsize; ++i) {
    if (IsGhost (i)) {
      // Ghosts holds the local indexes of the ghost elements
      // (the global one can be found directly)
      Ghosts.push_back (i);
      continue;
    }
    _CGlobal[i] = StartID;
    ++StartID;
  }
  
  CFLog(DEBUG_MIN, "MPICommPattern<DATA>: CMap: Remaining to translate: " << Ghosts.size() << " ghost elements...\n");
  // Now all CGlobal IDs are set for every non-ghost element
}
     
//////////////////////////////////////////////////////////////////////////////
 
template <typename DATA>
bool MPICommPattern<DATA>::HasCGlobal () const
{
  return _CGlobalValid;
}
   
//////////////////////////////////////////////////////////////////////////////
    
template <typename DATA>
void MPICommPattern<DATA>::BuildCGlobal ()
{
  if (!_IsIndexed) {
    CreateIndex();
  }
  
  // Determine maximum number of ghost elements
  const CFuint LocalGhostSize = GetGhostSize();
  CFuint MaxGhostSize = 0;
  
  Common::CheckMPIStatus(MPI_Allreduce (const_cast<CFuint *>(&LocalGhostSize),
					&MaxGhostSize, 1,
					MPIStructDef::getMPIType(&MaxGhostSize), 
					MPI_MAX, _Communicator));
  
  // Create a vector to do the translation for every ghost
  std::vector<IndexType> Ghosts;
  Ghosts.reserve (LocalGhostSize);
  
  // Fill local map
  BuildCGlobalLocal (Ghosts);
  
  // Now Ghosts has all the local indexes of our ghost elements
  
  cf_assert (Ghosts.size () == GetGhostSize());
  
  //
  // Here we ask all other CPU's to help translating our
  // ghost ID's to global continuous IDs
  //
  
  // Find out neigbours
  // TODO RING communicator
  
  int SendTo = (_CommRank == _CommSize - 1 ? 0 : _CommRank + 1);
  int ReceiveFrom = (_CommRank ? _CommRank - 1 : _CommSize - 1);
  
  CFLogDebugMin( "Starting ghost element lookup..."
		 " Receiving from " << ReceiveFrom << ", sending to "
		 << SendTo << "\n");
  
  MPI_Request SendRequest;
  MPI_Request ReceiveRequest;
  
  std::vector<IndexType> SendBuf (MaxGhostSize+1, 666);
  std::vector<IndexType> ReceiveBuf (MaxGhostSize+1, 666);
  
  // We store all the global IDs we want to translate in the
  // send buffer (first element is number of elements following)
  // and set the ghost flag to indicate that the number isn't translated
  // yet
  SendBuf[0] = Ghosts.size();
  for (CFuint i=0; i<static_cast<CFuint>(Ghosts.size()); ++i) {
    SendBuf[i+1] = SetFlag(LocalToGlobal(Ghosts[i]), _FLAG_GHOST);
  }
  
  for (int Round = 0; Round < _CommSize; ++Round) {
    int ssize = SendBuf[0]+1;
    Common::CheckMPIStatus(MPI_Isend (&SendBuf[0], ssize,
				      MPIStructDef::getMPIType(&SendBuf[0]),
				      SendTo, Round, _Communicator, &SendRequest));
    Common::CheckMPIStatus(MPI_Irecv (&ReceiveBuf[0], (int)ReceiveBuf.size(),
				      MPIStructDef::getMPIType(&ReceiveBuf[0]),
				      ReceiveFrom, Round, _Communicator, &ReceiveRequest));
    
    Common::CheckMPIStatus(MPI_Wait (&ReceiveRequest, MPI_STATUS_IGNORE));
    
    // Process receive buffer
    
    // It is impossible that we receive more elements than we agreed on.
    const CFuint EleCount = ReceiveBuf[0];
    
    cf_assert (EleCount <= MaxGhostSize);
    
    CFLogDebugMin( "BuildCMap: Translating " << EleCount << " elements...\n");
    
    for (CFuint i=0; i<EleCount; ++i)
      {
	const CFuint CurID = i+1;
	
	const IndexType CurVal = ReceiveBuf[CurID];
	
	// We have to map CurGlobalID to CGlobalID
	
	// If it is already mapped, skip
	if (!IsFlagSet(CurVal, _FLAG_GHOST)) {
	  CFLogDebugMax( "Skipping " << NormalIndex(CurVal) << "\n");
	  continue;
	}
	
        // We check if we have it
        IndexType LocalID = FindLocal (NormalIndex(CurVal));
	
        // If we don't have it, leave it
        if (LocalID == _NOT_FOUND) {
	  CFLogDebugMax( "Don't have " << NormalIndex(CurVal) << "\n");
	  continue;
	}
	
        // Now we have the local ID overhere, replace the
        // global one with the CGlobal one
	
        // For non-ghost entries CGlobal is already valid...
	
        cf_assert (!IsGhost (LocalID));
	
        CFLogDebugMax( "Translating " << NormalIndex (CurVal) << " by " 
		       << _CGlobal[LocalID] << "\n");
	
        ReceiveBuf[CurID] = _CGlobal[LocalID];
      }
    
    Common::CheckMPIStatus(MPI_Wait (&SendRequest, MPI_STATUS_IGNORE));
    
    SendBuf.swap (ReceiveBuf);
  }
  
  // After _CommSize rounds, we have back our own values
  // in SendBuf
  cf_assert (SendBuf.size()>=SendBuf[0]+1);
  cf_assert (SendBuf[0] == GetGhostSize ());
  cf_assert (Ghosts.size() == GetGhostSize ());
  
  // Now we have the global mapping ghosts and the translated
  // mapping in SendBuf...
  for (CFuint i=0; i<Ghosts.size(); ++i) {
    _CGlobal[Ghosts[i]] = SendBuf[i+1];
    cf_assert (!IsFlagSet (SendBuf[i+1], _FLAG_GHOST));
  }
  
  _CGlobalValid = true;
}
      
//////////////////////////////////////////////////////////////////////////////
 
template <typename DATA>
void MPICommPattern<DATA>::FreeCGlobal ()
{
  if (_CGlobalValid)
    InvalidateCGlobal ();
}

//////////////////////////////////////////////////////////////////////////////
 
template <typename DATA>
void MPICommPattern<DATA>::InvalidateCGlobal ()
{
  _CGlobalValid = false;
  std::swap (_CGlobal, std::vector<CFuint>());
}

//////////////////////////////////////////////////////////////////////////////
 
template <typename DATA>
typename MPICommPattern<DATA>::IndexType MPICommPattern<DATA>::LocalToCGlobal (IndexType LocalID) const
{
  cf_assert (_CGlobalValid);
  cf_assert (LocalID < _CGlobal.size());
  // always true // cf_assert (LocalID >= 0);
  return _CGlobal[LocalID];
}

//////////////////////////////////////////////////////////////////////////////
      
template <typename DATA>
inline typename MPICommPattern<DATA>::IndexType
MPICommPattern<DATA>::ClearFlag (IndexType Global, IndexType Flag) const
{
  return (Global & (~Flag));
}
      
template <typename DATA>
inline typename MPICommPattern<DATA>::IndexType
MPICommPattern<DATA>::SetFlag (IndexType Global, IndexType Flag) const
{
  return (Global | (Flag));
}

template <typename DATA>
inline bool MPICommPattern<DATA>::IsFlagSet (IndexType Global,
					     IndexType Flag) const
{
  return (Global & Flag);
}
      
//////////////////////////////////////////////////////////////////////////////

    /* ==================================================
    ///  Index functions
    /// ==================================================*/
   
    template <typename DATA>
    typename MPICommPattern<DATA>::IndexType
    MPICommPattern<DATA>::NormalIndex (IndexType Global) const
    {
      return ClearFlag (Global, _FLAG_DELETED|_FLAG_GHOST);
    }

    template <typename DATA>
    typename MPICommPattern<DATA>::IndexType
    MPICommPattern<DATA>::FindLocal (IndexType GlobalIndex) const
    {
      cf_assert (!IsFlagSet (GlobalIndex, _FLAG_GHOST|_FLAG_DELETED));
      
      if (_IsIndexed)
        {
	  typename TIndexMap::const_iterator Iter = _IndexMap.find(GlobalIndex);
	  if (Iter == _IndexMap.end ())
	    return _NOT_FOUND;
	  
	  return Iter->second;
        }
      
      CFLogDebugMax( "Warning: Using slow FindLocal (no index created) in"
		     " MPICommPattern<DATA>\n");
      for (CFuint i=0; i<size(); i++)
        {
	  IndexType I = _MetaData(i).GlobalIndex;
	  if (IsFlagSet (I, _FLAG_DELETED|_FLAG_GHOST))
	    continue;
	  if (NormalIndex(I)==GlobalIndex)
	    return i;
        }
      return _NOT_FOUND;
    }
      
      template <typename DATA>
      typename MPICommPattern<DATA>::IndexType
      MPICommPattern<DATA>::FindGhost (IndexType GlobalIndex) const
      {
	cf_assert (!IsFlagSet (GlobalIndex, _FLAG_GHOST|_FLAG_DELETED));
	
	if (_IsIndexed)
	  {
	    typename TGhostMap::const_iterator Iter = _GhostMap.find(GlobalIndex);
	    if (Iter == _GhostMap.end ())
	      return _NOT_FOUND;
	    return Iter->second;
	  }
	
	CFLogDebugMax( "Warning: Using slow FindGhost (no index created) in"
		       " MPICommPattern<DATA>\n");
	for (CFuint i=0; i<size (); i++)
	  {
	    IndexType I = _MetaData(i).GlobalIndex;
	    if (!IsFlagSet (I, _FLAG_GHOST))
	      continue;
	    if (IsFlagSet (I, _FLAG_DELETED))
	      continue;
	    if (NormalIndex(I)==GlobalIndex)
	      return i;
	  }
	return _NOT_FOUND;
      }

    template <typename DATA>
    void MPICommPattern<DATA>::DestroyIndex ()
    {
      cf_assert (_IsIndexed);

      _IndexMap.clear();
      _GhostMap.clear();

      _IsIndexed = false;
    }

    template <typename DATA>
    void MPICommPattern<DATA>::AddLocalIndex (IndexType Local, IndexType Global)
    {
      if (!_IsIndexed)
        return;
      cf_assert (!IsFlagSet (Global, _FLAG_DELETED|_FLAG_GHOST));
      cf_assert (_IndexMap.find(Global)==_IndexMap.end());

      _IndexMap[Global]=Local;
    }

    template <typename DATA>
    void MPICommPattern<DATA>::AddGhostIndex (IndexType Local, IndexType Global)
    {
      if (!_IsIndexed)
        return;

      cf_assert (_GhostMap.find(Global)==_GhostMap.end());

      _GhostMap[Global]=Local;
    }

//////////////////////////////////////////////////////////////////////////////
      
template <typename DATA>
void MPICommPattern<DATA>::CreateIndex()
{
  CFLog (VERBOSE, "MPICommPattern<DATA>::CreateIndex() => start\n" );
  
  cf_assert (!_IsIndexed);
  
  // Also erases memory
  _IndexMap.clear();
  _GhostMap.clear();
  
  for (unsigned int i=0; i < m_data->size(); ++i) {
    IndexType Global = _MetaData(i).GlobalIndex;
    
    if (IsFlagSet (Global, _FLAG_DELETED))
      continue;
    
    if (IsFlagSet (Global, _FLAG_GHOST))
      _GhostMap[NormalIndex(Global)]=i;
    else
      _IndexMap[NormalIndex(Global)]=i;
  }
  
  // cf_assert (_GhostMap.size ()==_GhostSize);
  // cf_assert (_IndexMap.size ()==_LocalSize);
  
  _IsIndexed=true;
  
  CFLog(VERBOSE, "MPICommPattern<DATA>::CreateIndex() => end\n" );
}
      
//////////////////////////////////////////////////////////////////////////////
      
template <typename DATA>
typename MPICommPattern<DATA>::IndexType
MPICommPattern<DATA>::LocalToGlobal (IndexType LocalIndex) const
{
  return NormalIndex(_MetaData(LocalIndex).GlobalIndex);
}
      
//////////////////////////////////////////////////////////////////////////////
  
/// Return true if the element with local ID LocaLID is a ghost element
template <typename DATA>
inline bool MPICommPattern<DATA>::IsGhost (IndexType LocalID) const
{
  return IsFlagSet (_MetaData(LocalID).GlobalIndex, _FLAG_GHOST);
}
     
//////////////////////////////////////////////////////////////////////////////
   
template <typename DATA>
typename MPICommPattern<DATA>::IndexType
MPICommPattern<DATA>::GlobalToLocal (IndexType GlobalIndex) const
{   
  // TODO: adapt for search functions
  //
  typename TIndexMap::const_iterator Iter = _IndexMap.find(GlobalIndex);
  if (Iter!=_IndexMap.end())
    return Iter->second;
  
  typename TGhostMap::const_iterator Iter2 = _GhostMap.find(GlobalIndex);
  if (Iter2!=_GhostMap.end())
    return Iter2->second;
  
  throw NotFoundException (FromHere(),"MPICommPattern<DATA>: NotFoundException");
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::Sync_BuildReceiveTypes ()
{
  Sync_BuildTypeHelper (_GhostReceiveList, _ReceiveTypes);
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::Sync_BroadcastNeeded ()
{
  IndexType MaxGhostSize = 0;
  
  // Determine needed buffer size
  Common::CheckMPIStatus (MPI_Allreduce (&_GhostSize, &MaxGhostSize, 1, 
					 MPIStructDef::getMPIType(&_GhostSize), 
					 MPI_MAX, _Communicator));
  
  // it is unsigned, so this is useless // cf_assert(MaxGhostSize>=0);
  
  if (!MaxGhostSize)
    return;     // No node has ghost points
  
  // Allocate storage
  const int StorageSize = MaxGhostSize+1;
  IndexType* Storage = new IndexType[StorageSize];
  
  typename TIndexMap::const_iterator Iter;
  
  // Broadcast needed points
  for (int RankTurn=0; RankTurn<_CommSize; RankTurn++)
    {
      if (RankTurn==_CommRank)
	{
	  //
	  // We send our list
	  //
	  Storage[0]=_GhostMap.size();
	  
	  int i=1;
	  for (typename TGhostMap::const_iterator Iter=_GhostMap.begin();
	       Iter!=_GhostMap.end(); Iter++)
	    Storage[i++]=Iter->first;
	  
	  Common::CheckMPIStatus(MPI_Bcast (Storage, StorageSize,  
					    MPIStructDef::getMPIType(&Storage[0]), 
					    RankTurn, _Communicator));
	}
      else
	{
	  //
	  // Time to receive the list
	  //
	  Common::CheckMPIStatus(MPI_Bcast (Storage, StorageSize, 	
					    MPIStructDef::getMPIType(&Storage[0]), RankTurn,
					    _Communicator));
	  
	  const IndexType Aantal = Storage[0];
	  cf_assert (Aantal <= MaxGhostSize);
	  
	  for (IndexType j=1; j<=Aantal; j++)
	    {
	      // Could use GlobalToLocal here, the exception-
	      // overhead would be too big.
	      Iter=_IndexMap.find( Storage[j]);
	      
	      if (Iter==_IndexMap.end())
		continue; // We don't have this one
	      
	      _GhostSendList[RankTurn].push_back(Iter->second);
	    }
	}
      
      //MPI_Barrier (_Communicator);
    }
  
  delete[] (Storage);
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::Sync_BuildReceiveList()
{
  IndexType MaxSendSize = 0;
  
  // Allocate bufferspace
  for (IndexType i=0; i< (IndexType) _CommSize; i++)
    MaxSendSize = std::max(MaxSendSize, static_cast<IndexType>(_GhostSendList[i].size()) );
  
  IndexType * ReceiveStorage = new IndexType[_CommSize*_GhostSize];
  IndexType * SendStorage = new IndexType[MaxSendSize];
  MPI_Request * Requests = new MPI_Request[_CommSize];
  
  // Post receives
  for (int i=0; i<_CommSize; i++)
    {
      if (i==_CommRank)
	{
	  Requests[i]=MPI_REQUEST_NULL;
	  continue;
	}
      
      Common::CheckMPIStatus(MPI_Irecv(&ReceiveStorage[i*_GhostSize], _GhostSize,
				       MPIStructDef::getMPIType(&ReceiveStorage[i*_GhostSize]), 
				       i, _MPI_TAG_BUILDGHOSTMAP, _Communicator,
				       &Requests[i]));
    }
  
  // Send ghost points
  for (int i=0; i<_CommSize; i++) {
    if (i==_CommRank) continue;
    
    IndexType j=0;
    for (typename std::vector<IndexType>::const_iterator iter = _GhostSendList[i].begin();
	 iter!=_GhostSendList[i].end (); iter++) {
      SendStorage[j++]=NormalIndex(_MetaData(*iter).GlobalIndex);
    }
    
    Common::CheckMPIStatus(MPI_Send (SendStorage, (int)_GhostSendList[i].size(),
				     MPIStructDef::getMPIType(SendStorage), i,
				     _MPI_TAG_BUILDGHOSTMAP, _Communicator));
  }
  
  // Wait receives
  while (true)
  {
    int Current = 0;
    MPI_Status Status;
    
    cf_assert (Requests != CFNULL);
    
    Common::CheckMPIStatus(MPI_Waitany (_CommSize, Requests, &Current, &Status));
    
    if (Current==MPI_UNDEFINED)
      break;
    
    cf_assert (Requests[Current]==MPI_REQUEST_NULL);
    
    int Aantal = 0;
    MPI_Get_count (&Status, MPIStructDef::getMPIType(&MaxSendSize), &Aantal);
    
    if (Aantal > _GhostSize) {
      CFLog(WARN, "MPICommPattern<DATA>::Sync_BuildReceiveList() => Aantal > _GhostSize : " 
	    << Aantal << " > " << _GhostSize << "\n");
      cf_assert(Aantal <= _GhostSize);
    }
    
    // Fill in receive list
    for (IndexType i=Current*_GhostSize; i<(Current*_GhostSize)+Aantal; i++)
      {
        typename TGhostMap::const_iterator Iter =
          _GhostMap.find(ReceiveStorage[i]);
	
        cf_assert (Iter!=_GhostMap.end());
	
        _GhostReceiveList[Current].push_back(Iter->second);
      }
  }
  
  delete[] (ReceiveStorage);
  delete[] (SendStorage);
  delete[] (Requests);
}

//////////////////////////////////////////////////////////////////////////////

///  Build the communication pattern for exchanging ghost points later 
template <typename DATA>
void MPICommPattern<DATA>::BuildGhostMapBcast()
{ 
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapBcast() => start\n");
  
  using namespace std;
  
  cf_assert (_InitMPIOK);
  
  // find the localID corresponding to ghost globalID in the donor process
  // counts for data to send/receive to/from each process
  m_sendCount.resize(_CommSize, 0);
  m_recvCount.resize(_CommSize, 0);
  m_sendDispl.resize(_CommSize, 0);
  m_recvDispl.resize(_CommSize, 0);
  
  CFuint nbLocalGhosts = _GhostMap.size();
  cf_assert(nbLocalGhosts > 0);
  
  CFuint maxNbLocalGhosts = 0;
  MPIError::getInstance().check
    ("MPI_Allreduce", "MPICommPattern<DATA>::BuildGhostMapBcast()",
     MPI_Allreduce(&nbLocalGhosts, &maxNbLocalGhosts, 1, 
		   MPIStructDef::getMPIType(&nbLocalGhosts), MPI_MAX, _Communicator));
  cf_assert(maxNbLocalGhosts > 0);
  
  int bcastSize =  maxNbLocalGhosts*2+1;
  // The following array contains:
  // 0) the number of ghost IDs of the broadcasting process 
  // 1) ghost global IDs in the receiving process
  // 2) donor rank from which ghosts are sent
  vector<CFuint> gGlobalDonorIDs(bcastSize);

  const CFuint elemsize = _ElementSize/sizeof(T);
  vector<CFuint> sendLocalIDs; 
  sendLocalIDs.reserve(elemsize*maxNbLocalGhosts); // overestimated size
  vector<CFuint> recvLocalIDs; 
  recvLocalIDs.reserve(elemsize*maxNbLocalGhosts); // overestimated size
  
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapBcast() => 1\n");
  
  for (CFuint root = 0; root < _CommSize; ++root) {
    if (root == _CommRank) {
      // ghosts have to be ordered by donor ID to be consistent with MPI_Alltoallv order
      // therefore we build a (multi) mapping to store pairs donorID->globalID
      // after the sorting, we can access directly the pairs ordered by donorID
      CFMultiMap<int,CFuint> donor2GhostGlobalID(_GhostMap.size());
      typename TGhostMap::const_iterator itr;
      for (itr = _GhostMap.begin(); itr != _GhostMap.end(); ++itr) {
	const CFuint globalID = itr->first;
	bool flag = false;
	const CFuint donorID = m_mapGhost2Donor->find(globalID, flag).first->second;
	cf_assert(flag);
	donor2GhostGlobalID.insert(donorID, globalID);
      }
      donor2GhostGlobalID.sortKeys();
      
      // first entry is the number of ghosts for the broadcasting process
      gGlobalDonorIDs[0] = nbLocalGhosts*2+1;
      CFuint countl = 1;
      const CFuint dsize = donor2GhostGlobalID.size();
      for (CFuint i = 0; i < dsize; ++i, countl+=2) {
	cf_assert(countl <= (CFuint) bcastSize);
	// store the ghost local IDs in root
	const CFuint globalID = donor2GhostGlobalID[i];
	const CFuint donorID  = donor2GhostGlobalID.getKey(i);
	gGlobalDonorIDs[countl]   = globalID;
	gGlobalDonorIDs[countl+1] = donorID;
	const CFuint localID = _GhostMap.find(globalID)->second;
	cf_assert(localID < size());
	recvLocalIDs.push_back(localID);
      }
    }
  
    // there can be room for optimization if we pass the right bcastSize instead of the max one
    MPIError::getInstance().check
      ("MPI_Bcast", "MPICommPattern<DATA>::BuildGhostMapBcast()",
       MPI_Bcast(&gGlobalDonorIDs[0], bcastSize, 
		 MPIStructDef::getMPIType(&gGlobalDonorIDs[0]), root, _Communicator)); 
    
    if (root != _CommRank) { 
      // store in each process information about 
      // 1) the destination processes (root)
      // 2) global IDs for the ghosts to send
      
      // we are considering ghosts, hence the root won't have to send anything to itself
      const CFuint rootSize = gGlobalDonorIDs[0];
      cf_assert(rootSize > 0);
      const CFuint rootNbGhosts = (rootSize-1)/2;
      CFuint countr = 1;
      for (CFuint i = 0; i < rootNbGhosts; ++i, countr+=2) {
	cf_assert(countr <= rootSize);
	if (gGlobalDonorIDs[countr+1] == _CommRank) {
	  // count how many ghosts*elemsize will be sent from _CommRank to root
	  m_sendCount[root] += elemsize;
	  // store the local IDs in _CommRank to be sent to root
	  const CFuint globalGhostID = gGlobalDonorIDs[countr];
	  const CFuint localGhostID = GlobalToLocal(globalGhostID);
	  cf_assert(localGhostID < size());
	  sendLocalIDs.push_back(localGhostID);
	}
      }
    }
  }
  
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapBcast() => 2\n");
  
  m_sendLocalIDs.resize(sendLocalIDs.size());
  copy(sendLocalIDs.begin(), sendLocalIDs.end(), m_sendLocalIDs.begin());
  
  m_recvLocalIDs.resize(recvLocalIDs.size());
  copy(recvLocalIDs.begin(), recvLocalIDs.end(), m_recvLocalIDs.begin());
  
  MPIError::getInstance().check
    ("MPI_Alltoall", "MPICommPattern<DATA>::BuildGhostMapBcast()",
     MPI_Alltoall(&m_sendCount[0], 1, MPIStructDef::getMPIType(&m_sendCount[0]),
		  &m_recvCount[0], 1, MPIStructDef::getMPIType(&m_recvCount[0]), 
		  _Communicator));
    
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapBcast() => 3\n");
 
  m_sendDispl[0] = 0;
  m_recvDispl[0] = 0;
  CFuint scount = m_sendCount[0];
  CFuint rcount = m_recvCount[0];
  for (CFuint i = 1; i < _CommSize; ++i) {
    m_sendDispl[i] = scount;
    // AL: not 100% sure about this
    if (m_recvCount[i] > 0) {
      m_recvDispl[i] = rcount;
    }
    scount += m_sendCount[i];
    rcount += m_recvCount[i];
  }
  
  cf_assert(m_sendLocalIDs.size()*elemsize == 
	    std::accumulate(m_sendCount.begin(), m_sendCount.end(), 0));
  cf_assert(m_recvLocalIDs.size()*elemsize == 
	    std::accumulate(m_recvCount.begin(), m_recvCount.end(), 0));
  
  CFLog(DEBUG_MIN, CFPrintContainer<vector<int> >("sendCount  = ", &m_sendCount));
  CFLog(DEBUG_MIN, CFPrintContainer<vector<int> >("recvCount  = ", &m_recvCount));
  CFLog(DEBUG_MIN, CFPrintContainer<vector<int> >("sendDispl  = ", &m_sendDispl));
  CFLog(DEBUG_MIN, CFPrintContainer<vector<int> >("recvDispl  = ", &m_recvDispl));
  
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapBcast() => end\n");
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::BuildGhostMapAllToAll()
{ 
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapAllToAll() => start\n");
  
  using namespace std;
  
  cf_assert (_InitMPIOK);
  
  // find the localID corresponding to ghost globalID in the donor process
  // counts for data to send/receive to/from each process
  m_sendCount.resize(_CommSize, 0);
  m_recvCount.resize(_CommSize, 0);
  m_sendDispl.resize(_CommSize, 0);
  m_recvDispl.resize(_CommSize, 0);
  
  CFuint nbLocalGhosts = _GhostMap.size();
  cf_assert(nbLocalGhosts > 0);
  
  // each processor communicates the global IDs of its ghosts to the donor process
  
  // ghosts have to be ordered by donor ID to be consistent with MPI_Alltoallv order
  // therefore we build a (multi) mapping to store pairs donorID->globalID
  // after the sorting, we can access directly the pairs ordered by donorID
  CFMultiMap<int,CFuint> donor2GhostGlobalID(_GhostMap.size());
  typename TGhostMap::const_iterator itr;
  for (itr = _GhostMap.begin(); itr != _GhostMap.end(); ++itr) {
    const CFuint globalID = itr->first;
    bool flag = false;
    const CFuint donorID = m_mapGhost2Donor->find(globalID, flag).first->second;
    cf_assert(flag);
    donor2GhostGlobalID.insert(donorID, globalID);
  }
  donor2GhostGlobalID.sortKeys();
  
  // The following array contains the global IDs corresponding to the ghost to receive
  const CFuint dsize = donor2GhostGlobalID.size();
  m_recvLocalIDs.resize(dsize);
  
  vector<int> sendCount(_CommSize, 0);
  vector<int> recvCount(_CommSize, 0);
  vector<int> sendDispl(_CommSize, 0);
  vector<int> recvDispl(_CommSize, 0);
  vector<CFuint> sendGhostGlobalIDs(dsize);
  
  for (CFuint i = 0; i < dsize; ++i) {
    const CFuint globalID = donor2GhostGlobalID[i];
    sendGhostGlobalIDs[i] = globalID;
    const CFuint donorID  = donor2GhostGlobalID.getKey(i);
    // donor is always != current rank
    cf_assert(donorID != _CommRank); 
    sendCount[donorID]++;
    const CFuint localID = _GhostMap.find(globalID)->second;
    cf_assert(localID < size());
    m_recvLocalIDs[i] = localID;
  }
  cf_assert(sendCount[_CommRank] == 0);
  
  MPIError::getInstance().check
    ("MPI_Alltoall", "MPICommPattern<DATA>::BuildGhostMapAllToAll()",
     MPI_Alltoall(&sendCount[0], 1, MPIStructDef::getMPIType(&sendCount[0]),
		  &recvCount[0], 1, MPIStructDef::getMPIType(&recvCount[0]), 
		  _Communicator));
  
  cf_assert(recvCount[_CommRank] == 0);
  
  sendDispl[0] = 0;
  recvDispl[0] = 0;
  CFuint scount = sendCount[0];
  CFuint rcount = recvCount[0];
  for (CFuint i = 1; i < _CommSize; ++i) {
    sendDispl[i] = scount;
    // AL: not 100% sure about this
    if (recvCount[i] > 0) {
      recvDispl[i] = rcount;
    }
    scount += sendCount[i];
    rcount += recvCount[i];
  }
  
  cf_assert(scount == m_recvLocalIDs.size());
  cf_assert(rcount == std::accumulate(recvCount.begin(), recvCount.end(), 0));
  
  // during the first MPI_Alltoallv, each rank sends the global IDs to the rank that 
  // will send the updated ghost state/node data back at the next MPI_Alltoallv
  
  vector<CFuint> recvGhostGlobalIDs(rcount);
  
  MPIError::getInstance().check
    ("MPI_Alltoallv", "MPICommPattern<DATA>::BuildGhostMapAllToAll()",
     MPI_Alltoallv(&sendGhostGlobalIDs[0], &sendCount[0], &sendDispl[0], 
		   MPIStructDef::getMPIType(&sendGhostGlobalIDs[0]), 
		   &recvGhostGlobalIDs[0], &recvCount[0], &recvDispl[0], 
		   MPIStructDef::getMPIType(&recvGhostGlobalIDs[0]),
		   _Communicator));
  
  m_sendLocalIDs.resize(rcount);
  for (CFuint i = 0; i < rcount; ++i) {
    const CFuint globalGhostID = recvGhostGlobalIDs[i];
    const CFuint localGhostID = GlobalToLocal(globalGhostID);
    cf_assert(localGhostID < size());
    m_sendLocalIDs[i] = localGhostID;
  }
  
  const CFuint elemsize = _ElementSize/sizeof(T);
  for (CFuint i = 0; i < _CommSize; ++i) {
    m_sendCount[i]  = recvCount[i]*elemsize;
    m_sendDispl[i] = recvDispl[i]*elemsize;
    m_recvCount[i]  = sendCount[i]*elemsize;
    m_recvDispl[i] = sendDispl[i]*elemsize;
  }
  
  CFLog(DEBUG_MIN, CFPrintContainer<vector<int> >("sendCount  = ", &m_sendCount));
  CFLog(DEBUG_MIN, CFPrintContainer<vector<int> >("recvCount  = ", &m_recvCount));
  CFLog(DEBUG_MIN, CFPrintContainer<vector<int> >("sendDispl  = ", &m_sendDispl));
  CFLog(DEBUG_MIN, CFPrintContainer<vector<int> >("recvDispl  = ", &m_recvDispl));
  
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapAllToAll() => end\n");
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::BuildGhostMapOld()
{ 
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapOld() => start\n");
  
  cf_assert (_InitMPIOK);
  cf_assert (_GhostSendList.size()==static_cast<CFuint>(_CommSize));
  cf_assert (_GhostReceiveList.size()==static_cast<CFuint>(_CommSize));
  
   // Clear old mapping
  for (int j=0; j<_CommSize; j++) {
    _GhostSendList[j].clear();
    _GhostReceiveList[j].clear();
  }
  
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapOld() => 1\n");
  // Broadcast needed points
  Sync_BroadcastNeeded ();
  
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapOld() => 2\n");
    // Build send datatype
  Sync_BuildSendTypes();
  
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapOld() => 3\n");
  // Now building receive lists
  Sync_BuildReceiveList ();
  
  // Check if all ghost elements were found...
  IndexType GhostFound = 0;
  for (int i=0; i<_CommSize; i++) {
    GhostFound+=_GhostReceiveList[i].size();
  }
  
  cf_assert (_GhostSize == _GhostMap.size());
  if (GhostFound != _GhostSize)
    {
      // Error: we don't have all the ghost points
      CFLog(DEBUG_MIN, "Not all ghost points were found! Starting investigation\n");
      
      std::set<CFuint> Ghosts;
      std::set<CFuint> Receives;
      std::set<CFuint> Missing;
      
      typename TGhostMap::const_iterator Iter;
      
      for (Iter=_GhostMap.begin(); Iter!=_GhostMap.end(); ++Iter)
	Ghosts.insert(Iter->first);
      
      for (int i=0; i<_CommSize; ++i)
	std::copy(_GhostReceiveList[i].begin(), _GhostReceiveList[i].end(),
		  std::inserter(Receives, Receives.begin()));
      
      std::set_difference(Ghosts.begin(), Ghosts.end(), Receives.begin(),
			  Receives.end(), std::inserter(Missing, Missing.begin()));
      
      std::ostringstream S;
      S << "Missing ghost elements (globalID): ";
      for (std::set<CFuint>::const_iterator I = Missing.begin();
	   I!=Missing.end(); ++I)
	S << *I << " ";
      S << "\n";
      
      throw NotFoundException(FromHere(), S.str().c_str());
    }
  
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapOld() => 4\n");
  // Build receive datatype
  Sync_BuildReceiveTypes ();
  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapOldp() => 5\n");
  
#ifdef CF_ENABLE_PARALLEL_DEBUG
  WriteCommPattern ();
#endif

  CFLog(VERBOSE, "MPICommPattern<DATA>::BuildGhostMapOld() => end\n");
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::Sync_BuildTypeHelper 
(const std::vector<std::vector<IndexType> > & V,
 std::vector<MPI_Datatype> & MPIType) const
{
  
#ifdef HAVE_MPI_TYPE_GET_TRUE_EXTENT
  // Safety check
  MPI_Aint dummy, extent;
  Common::CheckMPIStatus(MPI_Type_get_true_extent (_BasicType, &dummy,&extent));
  cf_assert ((size_t) extent == ElementSize);
#endif
  
  for (int i=0; i<_CommSize; i++)
    if (MPIType[i]!=MPI_DATATYPE_NULL)
      MPI_Type_free (&MPIType[i]);
  
  
  IndexType MaxSize = 0;
  for (int i=0; i<_CommSize; i++)
    MaxSize = std::max(MaxSize, static_cast<IndexType>(V[i].size ()));
  
  int * Offset = new int[MaxSize];
  int * Length = new int[MaxSize];
  
  for (int i=0; i<_CommSize; i++)
    {
      if (!V[i].size())
	continue;
      
      for (CFuint j=0; j<V[i].size(); j++)
	{
	  Length[j]=1;
	  Offset[j]=V[i][j];
	}
      
      CheckMPIStatus (MPI_Type_indexed ((int)V[i].size(),Length,Offset,_BasicType, &MPIType[i]));
      CheckMPIStatus (MPI_Type_commit (&MPIType[i]));
    }
  
  delete[] (Offset);
  delete[] (Length);
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::Sync_BuildSendTypes ()
{
  Sync_BuildTypeHelper (_GhostSendList, _SendTypes);
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::reserve (IndexType reservesize,
				    CFuint elementSize,
				    const std::string nspaceName)
{      
  CFLog ( DEBUG_MIN, "MPICommPattern<DATA>::reserve() => _InitMPIOK  = " 
	  << _InitMPIOK << "\n" );
  CFLog ( DEBUG_MIN, "MPICommPattern<DATA>::reserve() => elementSize = " 
	  << elementSize << "\n" );
  CFLog ( DEBUG_MIN, "MPICommPattern<DATA>::reserve() => reservesize = " 
	  << reservesize << "\n" );
  
  if (!_InitMPIOK) {
    m_data->free();
    _MetaData.free();
    CFLog(DEBUG_MIN, "MPICommPattern<DATA>::reserve() => elementSize/m_data->sizeFactor() = " 
	  << elementSize/m_data->sizeFactor() << "\n" );
    
    m_data->initialize(T(), 0, elementSize/m_data->sizeFactor());
    _MetaData.initialize(DataType(), 0);
    
    cf_assert(_ElementSize == 0);
    _ElementSize = elementSize;
    InitMPI(nspaceName);
  }
  
  if (reservesize <= m_data->size())
    return;
  
  IndexType growBy =  reservesize - m_data->size();
  CFLogDebugMin( "MPICommPattern<DATA>::reserve() => reservesize ="
		 << reservesize << ", growing by " << growBy
		 << ", current size=" << m_data->size() << "\n");
  grow (growBy);
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::grow (IndexType growBy)
{ 
  IndexType OldSize = m_data->size();
  IndexType NewSize;
  
  if (growBy) {
    CFLog(DEBUG_MIN, "MPICommPattern<DATA>::growBy " << OldSize+growBy  << "\n");
    m_data->resize(OldSize+growBy);
  }
  else {
    CFLog(DEBUG_MIN, "MPICommPattern<DATA>::grow " << OldSize+growBy  << "\n");
    m_data->grow();
  }
  
  NewSize = m_data->size();
  _MetaData.Resize(NewSize);
  
  cf_assert (NewSize>OldSize);
  cf_assert (_MetaData.size()>= m_data->size());
  
  // Now we have additional storage starting at CurrentSize
  // Walk current free list to prevent fragmentation
  IndexType Current = _NextFree;
  while (Current!=_NO_MORE_FREE &&
	 _MetaData(Current).GlobalIndex!=_NO_MORE_FREE)
    Current=_MetaData(Current).GlobalIndex;
  
  // Now current points to the last free block.
  
  // Link new free blocks
  for (IndexType i=OldSize; i<(NewSize-1); i++)
    _MetaData(i).GlobalIndex=i+1;
  _MetaData(NewSize-1).GlobalIndex=_NO_MORE_FREE;
  
  // Only thing left is linking last old free block with the start of the
  // new list
  if (Current!=_NO_MORE_FREE)
    {
      cf_assert(_MetaData(Current).GlobalIndex==_NO_MORE_FREE);
      _MetaData(Current).GlobalIndex=OldSize;
    }
  
  if (_NextFree==_NO_MORE_FREE)
    _NextFree=OldSize;
  
  cf_assert (_MetaData.size() >= m_data->size());
}
      
//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
typename MPICommPattern<DATA>::IndexType MPICommPattern<DATA>::AllocNext()
{
  // Check to see if we reached the limit of our base IndexType
  if (IsFlagSet (size()+1, _FLAG_DELETED|_FLAG_GHOST))
  {
    CFLog(DEBUG_MIN, "Limit of IndexType reached!!!!!\n");
    return _NO_MORE_FREE;
  }
  
  if (_NextFree==_NO_MORE_FREE)
  {
    // Must grow
    grow ();
  }
  
  cf_assert (_NextFree!=_NO_MORE_FREE);
  
  IndexType NewID = _NextFree;
  _NextFree = _MetaData(NewID).GlobalIndex; // From free list
  
  return NewID;
}

//////////////////////////////////////////////////////////////////////////////


    /// Dump the local contents
    /// (non-global operation)
    /// Can be called before buildmap was called
    template <typename DATA>
    void MPICommPattern<DATA>::DumpContents ()
    {
      std::ostringstream S;
      S << "parvector_cont.dump." << _CommRank;

      std::ofstream Out (S.str().c_str());

      Out << "Content dump for rank " << _CommRank << "\n";

      for (CFuint i=0; i<size(); ++i)
        {
    int Global = LocalToGlobal(i);
    Out << i << " " << Global;
    if (IsFlagSet (_MetaData(i).GlobalIndex, _FLAG_GHOST))
      Out << " [ghost]";
    Out << "\n";
        }
      Out << "Size = " << size() ;
      Out << " [_GhostSize = " << _GhostSize ;
      Out << ", _LocalSize = " << _LocalSize << "]\n";
    }



//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::DumpInternalData ()
{
  std::ostringstream S;
  S << "parvector.dump." << _CommRank;
  
  std::ofstream Out (S.str().c_str());
  MPI_Barrier(_Communicator);
  
  if (!_CommRank)
    {
      Out << "Flags : \n";
      Out << " _NO_MORE_FREE: " << _NO_MORE_FREE <<"\n";
      Out << " _FLAG_DELETED: " << _FLAG_DELETED << "\n";
      Out << " _FLAG_GHOST:   " << _FLAG_GHOST << "\n";
      Out << "\n";
    }
  
  for (int j=0; j<_CommSize; j++) {
    if (j==_CommRank) {
      Out << "Ghost map for node " << _CommRank << "\n";
      Out <<  "--------------------" << "\n";
      // Write out ghost map
      for (int i=0; i<_CommSize; i++) {
	Out << _CommRank << ": Ghost send list to node " << i << ": ";
	for (typename std::vector<IndexType>::const_iterator iter=_GhostSendList[i].begin();
	     iter!=_GhostSendList[i].end(); iter++)
	  Out <<  *iter << "(" <<
	    NormalIndex(_MetaData(*iter).GlobalIndex) << ") ";
	Out << "Receive: ";
	for (typename std::vector<IndexType>::const_iterator
	       iter=_GhostReceiveList[i].begin();
	     iter!=_GhostReceiveList[i].end(); iter++) {
	  Out << *iter << "(" <<
	    NormalIndex(_MetaData(*iter).GlobalIndex) << ") ";
	}
	
	Out <<  "\n";
      }
      Out <<  "Indexmap: ";
      for (typename TIndexMap::const_iterator Iter=_IndexMap.begin(); 
	   Iter!=_IndexMap.end(); Iter++) {
	Out << Iter->first << " " ;
      }
      Out << "\n\n";
    }
    MPI_Barrier(_Communicator);
  }
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::BeginSync ()
{
  cf_assert (_InitMPIOK);
  
  //
  // TODO: dit kan beter
  //   Onnodig om over de hele lijst te lopen
  //   (anders niet schaalbaar in functie v/ aantal nodes)
  //
  
  // Post receives
  for (int i=0; i<_CommSize; i++)
    {
      if (i==_CommRank)
	continue;
      
      //
      // Idea: use persistent requests
      // (try to measure performance improvement)
      //
      if (!_GhostReceiveList[i].empty())
	{
	  Common::CheckMPIStatus(MPI_Irecv (m_data->ptr(), 1, _ReceiveTypes[i], i,
					    _MPI_TAG_SYNC, _Communicator, &_ReceiveRequests[i]));
	}
      
      if (!_GhostSendList[i].empty())
	{
	  Common::CheckMPIStatus(MPI_Isend (m_data->ptr(), 1, _SendTypes[i], i, _MPI_TAG_SYNC,
					    _Communicator, &_SendRequests[i]));
	}
    }
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::EndSync ()
{
  cf_assert (_InitMPIOK);
  
  // In feite is volgende niet nodig aangezien receives niet kunnen
  // klaar zijn alvorens de sends klaar zijn
  
  // Misschien 1 grote array gebruiken om 1 MPI_Waitall te kunnen doen
  Common::CheckMPIStatus(MPI_Waitall (_CommSize, &_SendRequests[0], MPI_STATUSES_IGNORE));
  Common::CheckMPIStatus(MPI_Waitall (_CommSize, &_ReceiveRequests[0], MPI_STATUSES_IGNORE));
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
typename MPICommPattern<DATA>::IndexType
MPICommPattern<DATA>::AddGhostPoint (IndexType GlobalIndex)
{
  typename TGhostMap::const_iterator Iter = _GhostMap.find(GlobalIndex);
  
  if (Iter!=_GhostMap.end())
    throw DoubleElementException
      (FromHere(), "MPICommPattern<DATA>: AddGhostPoint: DoubleElementException");
  
  //
  // Alternative:
  //    return index for the local point if a ghost point for a
  //    local point is requested
  //
  typename TIndexMap::const_iterator Iter2 = _IndexMap.find(GlobalIndex);
  if (Iter2!=_IndexMap.end())
    throw DoubleElementException
      (FromHere(), "MPICommPattern<DATA>: AddGhostPoint: DoubleElementException");
  
  IndexType NewLocalID = AllocNext ();
  
  cf_assert (NewLocalID!=_NO_MORE_FREE);
  //  cf_assert (GlobalIndex>=0);
  
  _MetaData(NewLocalID).GlobalIndex = SetFlag(GlobalIndex, _FLAG_GHOST);
  _GhostSize++;
  
  _GhostMap[GlobalIndex]=NewLocalID;
  
  CFLogDebugMax( "AddGhostPoint: local=" << NewLocalID << ", global=" <<
		 GlobalIndex << "\n");
  
  return NewLocalID;
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
typename MPICommPattern<DATA>::IndexType
MPICommPattern<DATA>::AddLocalPoint (IndexType GlobalIndex)
{
  IndexType NewLocalID = AllocNext ();
  
  if (NewLocalID == _NO_MORE_FREE )
    throw StorageException
      (FromHere(), "MPICommPattern<DATA>: AddLocalPoint: No more free space");
  
  cf_assert (NewLocalID!=_NO_MORE_FREE);
  
  _MetaData(NewLocalID).GlobalIndex = ClearFlag(GlobalIndex, _FLAG_GHOST);
  _LocalSize++;
  
  typename TIndexMap::const_iterator Iter = _IndexMap.find(GlobalIndex);
  if (Iter != _IndexMap.end())
    throw DoubleElementException
      (FromHere(), "MPICommPattern<DATA>: AddLocalPoint: DoubleElementException!");
  
  _IndexMap[GlobalIndex]=NewLocalID;
  
  CFLogDebugMax( "Add localpoint: local " << NewLocalID << ", global " <<
		 GlobalIndex << "\n");
  
  return NewLocalID;
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
typename MPICommPattern<DATA>::IndexType
MPICommPattern<DATA>::GetGhostSize () const
{
  // cf_assert (_GhostMap.size()==_GhostSize);
  return _GhostSize;
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
typename MPICommPattern<DATA>::IndexType
MPICommPattern<DATA>::GetLocalSize () const
{
  //cf_assert (_IndexMap.size()<=_LocalSize);
  return _LocalSize;
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
typename MPICommPattern<DATA>::IndexType
MPICommPattern<DATA>::GetGlobalSize () const
{
  cf_assert (_InitMPIOK);
  
  IndexType Total = 0;
  IndexType Local = GetLocalSize();
  
  Common::CheckMPIStatus(MPI_Allreduce 
			 (&Local, &Total, 1, 
			  MPIStructDef::getMPIType(&Local), MPI_SUM,
			  _Communicator));
  
  return Total;
}

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::DoneMPI ()
{
#ifndef NDEBUG
  cf_assert (_InitMPIOK == true);
  _InitMPIOK = false;
#endif
  //    MPI_Waitall (_CommSize, _ReceiveRequests, MPI_STATUSES_IGNORE);
  //    MPI_Waitall (_CommSize, _ReceiveRequests, MPI_STATUSES_IGNORE);
  
  for (int i = 0; i <_CommSize; i++) {
    if (_SendTypes[i]!=MPI_DATATYPE_NULL) {
      MPI_Type_free (&_SendTypes[i]);
    }
    if (_ReceiveTypes[i]!=MPI_DATATYPE_NULL) {
      MPI_Type_free (&_ReceiveTypes[i]);
    }
  }
  
  CFLogDebugMin( "MPICommPattern<DATA>::DoneMPI\n");
}

//////////////////////////////////////////////////////////////////////////////
      
template <typename DATA>
void MPICommPattern<DATA>::InitMPI (const std::string nspaceName)
{
  //TODO: set errhandler: MPI_Comm_set_errhandler / MPI_Errhandler_set
  //
  // No need for error checking, default MPI error handling = abort
  //
  // get the communicator
  _Communicator = PE::GetPE().GetCommunicator(nspaceName);
  
  cf_assert (_InitMPIOK == false);
  _InitMPIOK = true;
  
  MPI_Comm_rank (_Communicator, &_CommRank);
  MPI_Comm_size (_Communicator, &_CommSize);
  
  _GhostSendList.resize (_CommSize);
  _GhostReceiveList.resize (_CommSize);
  _SendTypes.resize (_CommSize);
  _ReceiveTypes.resize (_CommSize);
  _ReceiveRequests.resize (_CommSize);
  _SendRequests.resize (_CommSize);
  
  for (int i=0; i<_CommSize; i++) {
    _SendTypes[i]=_ReceiveTypes[i]=MPI_DATATYPE_NULL;
    _ReceiveRequests[i]=MPI_REQUEST_NULL;
    _SendRequests[i]=MPI_REQUEST_NULL;
  }
  
  // Need to set the basic type
  if (_ElementSize != sizeof (T)) {
    // We have to provide our own...
    // TODO: !!! this can cause trouble !!!
    MPI_Type_contiguous (_ElementSize, MPI_BYTE, &_BasicType);
    MPI_Type_commit (&_BasicType);
  }
  else {
    T dummy = T();
#ifdef HAVE_MPI_TYPE_DUP
    MPI_Type_dup (MPIStructDef::getMPIType(&dummy), &_BasicType);
    MPI_Type_commit (&_BasicType);
#else
    _BasicType = MPIStructDef::getMPIType(&dummy);
#endif
  }
  
  CFLogDebugMin( "MPICommPattern<DATA>::InitMPI\n");
}
      
//////////////////////////////////////////////////////////////////////////////
      
template <typename DATA>
MPICommPattern<DATA>::~MPICommPattern ()
{
  DoneMPI();
}      

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
MPICommPattern<DATA>::MPICommPattern (const std::string nspaceName, 
				      DATA* data, const T & Init, CFuint Size, CFuint ESize)
  : _ElementSize(ESize), _LocalSize(0), _GhostSize(0),
    _NextFree(_NO_MORE_FREE), m_data(data), _MetaData(DataType(), 0),
    _IsIndexed(false), _InitMPIOK(false), _CGlobalValid(false)
{
  if (ESize > 0) {
    InitMPI (nspaceName);
  }
}

//////////////////////////////////////////////////////////////////////////////

#ifdef CF_ENABLE_PARALLEL_DEBUG
template <typename DATA>
void MPICommPattern<DATA>::WriteCommPattern () const
{
  std::vector<CFuint> Send(_CommSize), Receive(_CommSize);
  
  for (CFuint i=0; i<_GhostSendList.size(); ++i)
    {
      Send[i] = _GhostSendList[i].size();
      Receive[i] = _GhostReceiveList[i].size();
    }
  
  WriteCommPatternHelper (_Communicator,
			  GetLocalSize(), GetGhostSize(),
			  Send, Receive);
}
#endif

//////////////////////////////////////////////////////////////////////////////

template <typename DATA>
void MPICommPattern<DATA>::synchronize()
{ 
  CFLog(VERBOSE, "MPICommPattern<DATA>::synchronize() => start\n");
  
  if (_CommSize > 1) {
    using namespace std;
    
    T dummy = 0.;
    const CFuint elemsize = _ElementSize/sizeof(T);
    
    // allocate the send and recv buffers
    m_sendBuf.resize(m_sendLocalIDs.size()*elemsize);
    cf_assert(m_sendBuf.size() > 0);
    
    m_recvBuf.resize(m_recvLocalIDs.size()*elemsize);
    cf_assert(m_recvBuf.size() > 0);
    
    // send local IDs stores the local IDs of the locally updatable DOFs to send 
    const CFuint totalSize = size()*elemsize;
    
    CFLog(VERBOSE, "MPICommPattern<DATA>::synchronize() => 1\n");
    
    CFuint scounter = 0;
    for (CFuint i = 0; i < m_sendLocalIDs.size(); ++i) {
      const CFuint startLocalID = m_sendLocalIDs[i]*elemsize;
      for (CFuint e = 0; e < elemsize; ++e, ++scounter) {
	const CFuint localID = startLocalID+e;
	cf_assert(localID < totalSize);
	m_sendBuf[scounter] = m_data->ptr()[localID]; // localID must be < nbGhosts
      }
    }
    
    CFLog(VERBOSE, "MPICommPattern<DATA>::synchronize() => 2\n");
    
    MPIError::getInstance().check
      ("MPI_Alltoallv", "MPICommPattern<DATA>::synchronize()",
       MPI_Alltoallv(&m_sendBuf[0], &m_sendCount[0], &m_sendDispl[0], 
		     MPIStructDef::getMPIType(&dummy), 
		     &m_recvBuf[0], &m_recvCount[0], &m_recvDispl[0], 
		     MPIStructDef::getMPIType(&dummy),
		     _Communicator));
    
    CFLog(VERBOSE, "MPICommPattern<DATA>::synchronize() => 3\n");
    
    CFuint rcounter = 0;
    for (CFuint i = 0; i < m_recvLocalIDs.size(); ++i) {
      const CFuint startLocalID = m_recvLocalIDs[i]*elemsize;
      for (CFuint e = 0; e < elemsize; ++e, ++rcounter) {
	const CFuint localID = startLocalID+e;
	cf_assert(localID < totalSize);
	m_data->ptr()[localID] = m_recvBuf[rcounter];
      }
    }
  }
  
  CFLog(VERBOSE, "MPICommPattern<DATA>::synchronize() => end\n");
}

//////////////////////////////////////////////////////////////////////////////

  } // Common

} // COOLFluiD

//////////////////////////////////////////////////////////////////////////////

// #include "Common/MPICommPattern.ci"

//////////////////////////////////////////////////////////////////////////////

#endif

