/*
Copyright CEA (2014).
Copyright Université Paris XI (2014).

Contributor: Yann Leprince <yann.leprince@ylep.fr>.

This file is part of highres-cortex, a collection of software designed
to process high-resolution magnetic resonance images of the cerebral
cortex.

This software is governed by the CeCILL licence under French law and
abiding by the rules of distribution of free software. You can use,
modify and/or redistribute the software under the terms of the CeCILL
licence as circulated by CEA, CNRS and INRIA at the following URL:
<http://www.cecill.info/>.

As a counterpart to the access to the source code and rights to copy,
modify and redistribute granted by the licence, users are provided only
with a limited warranty and the software's author, the holder of the
economic rights, and the successive licensors have only limited
liability.

In this respect, the user's attention is drawn to the risks associated
with loading, using, modifying and/or developing or reproducing the
software by the user in light of its specific status of scientific
software, that may mean that it is complicated to manipulate, and that
also therefore means that it is reserved for developers and experienced
professionals having in-depth computer knowledge. Users are therefore
encouraged to load and test the software's suitability as regards their
requirements in conditions enabling the security of their systems and/or
data to be ensured and, more generally, to use and operate it in the
same conditions as regards security.

The fact that you are presently reading this means that you have had
knowledge of the CeCILL licence and that you accept its terms.
*/

#include <cassert>
#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <set>

#include <boost/heap/d_ary_heap.hpp>
#include <boost/iterator/indirect_iterator.hpp>

namespace
{

const int debug_output = 0;

inline bool approx_equal(float x, float y)
{
  return std::abs(x - y) < 0.1f;
}

template <typename Tlabel> class Region;
template <typename Tlabel>
std::ostream& operator << (std::ostream& stream, const Region<Tlabel>& region);

template <typename Tlabel>
class Region
{
  friend std::ostream& operator << <Tlabel> (std::ostream&, const Region<Tlabel>&);

public:
  typedef boost::indirect_iterator<typename std::set<Region*>::const_iterator>
  const_neighbour_iterator;
  typedef boost::indirect_iterator<typename std::set<Region*>::iterator>
  neighbour_iterator;

  Region(const Tlabel region_label, const float initial_quality)
    : m_label(region_label), m_neighbours(), m_quality(initial_quality)
  {
  }

  Region(const Region& other)
    : m_label(other.m_label),
      m_neighbours(other.m_neighbours),
      m_quality(other.m_quality)
  {
    // Ensure reciprocity of the neighbourhood relationship
    std::for_each(m_neighbours.begin(), m_neighbours.end(),
                  std::bind2nd(std::mem_fun(&Region::add_forward_neighbour), this));
  }

  virtual ~Region()
  {
    // Remove this disappearing node from the neighbourhood of its neighbours
    // (prevent broken edges in the region neighbourhood graph)
    std::for_each(m_neighbours.begin(), m_neighbours.end(),
                  std::bind2nd(std::mem_fun(&Region::remove_forward_neighbour), this));
  }

  Tlabel label() const
  {
    return m_label;
  }

  void add_neighbour(Region<Tlabel>& neighbour_region)
  {
    assert(&neighbour_region != this);
    m_neighbours.insert(&neighbour_region);
    neighbour_region.m_neighbours.insert(this);
  }

  float quality() const
  {
    return m_quality;
  }

  void update_quality(const float new_quality)
  {
    m_quality = new_quality;
  }

  const_neighbour_iterator neighbours_begin() const
  { return m_neighbours.begin(); };
  const_neighbour_iterator neighbours_end() const
  { return m_neighbours.end(); };
  neighbour_iterator neighbours_begin()
  { return m_neighbours.begin(); };
  neighbour_iterator neighbours_end()
  { return m_neighbours.end(); };

  bool operator == (const Region& other) const
  {
    return this == &other;
  }

  bool operator != (const Region& other) const
  {
    return this != &other;
  }

private:
  void add_forward_neighbour(Region<Tlabel>* neighbour_region)
  {
    m_neighbours.insert(neighbour_region);
  }

  void remove_forward_neighbour(Region<Tlabel>* neighbour_region)
  {
    m_neighbours.erase(neighbour_region);
  }

  const Tlabel m_label;
  std::set<Region*> m_neighbours;
  float m_quality;
};

template <typename Tlabel, typename CacheType>
class CachingRegion : public Region<Tlabel>
{
public:
  CachingRegion(const Tlabel region_label,
                float initial_quality,
                const CacheType& cache)
    : Region<Tlabel>(region_label, initial_quality),
      m_cache(cache)
  {
  }

  CachingRegion(const CachingRegion<Tlabel, CacheType>& other)
    : Region<Tlabel>(other),
      m_cache(other.m_cache)
  {
  }

  const CacheType& cache() const { return m_cache; };
  void set_cache(const CacheType& new_cache) { m_cache = new_cache; };

  bool traversing() const
  {
    return m_cache.touches_CSF() && m_cache.touches_white();
  };

private:
  CacheType m_cache;
};

template <typename Tlabel>
std::ostream& operator << (std::ostream& stream, const Region<Tlabel>& region)
{
  stream << "Region@" << &region << " (" << "label=" << region.label()
         << ", quality=" << region.quality()  << ", #neighbours="
         << region.m_neighbours.size();
  return stream;
}

template <typename Tlabel, typename CacheType>
class RegionInQueue : public CachingRegion<Tlabel, CacheType>
{
public:
  typedef boost::heap::d_ary_heap<RegionInQueue<Tlabel, CacheType>,
                                  boost::heap::arity<2>,
                                  boost::heap::mutable_<true> > RegionQueue;
  typedef typename RegionQueue::handle_type Handle;

  RegionInQueue(const Tlabel region_label, const float initial_quality,
                const CacheType& cache)
    : CachingRegion<Tlabel, CacheType>(region_label, initial_quality, cache),
      m_handle()
  {
  }

  RegionInQueue(const RegionInQueue<Tlabel, CacheType>& other)
    : CachingRegion<Tlabel, CacheType>(other), m_handle(other.m_handle)
  {
  }

  bool operator < (const RegionInQueue<Tlabel, CacheType>& other) const
  {
    // Be careful: Boost's heap is a MAX-heap (priority queue), whereas we want
    // a MIN-heap (lower quality gets out first). Therefore, the logic of this
    // test is reversed: A < B means region A is of HIGHER quality than region
    // B.
    if(this->traversing() && !other.traversing())
      return true;
    else if(!this->traversing() && other.traversing())
      return false;
    else
     return this->quality() > other.quality();
  }

  void set_handle(const Handle& handle)
  {
    m_handle = handle;
  }

  const Handle& handle()
  {
    return m_handle;
  }

private:
  Handle m_handle;
};

}; // end of anonymous namespace

namespace yl
{

template <typename Tlabel, class RegionQualityCriterion>
IterativeRegionMerger<Tlabel, RegionQualityCriterion>::
IterativeRegionMerger(const LabelVolume<Tlabel>& label_vol,
                      const RegionQualityCriterion& criterion,
                      const int verbosity)
  : m_label_volume(label_vol), m_criterion(criterion),
    m_verbosity(verbosity)
{
}

template <typename Tlabel, class RegionQualityCriterion>
void
IterativeRegionMerger<Tlabel, RegionQualityCriterion>::
setVerbose(const int verbosity)
{
  m_verbosity = verbosity;
}

template <typename Tlabel, class RegionQualityCriterion>
void
IterativeRegionMerger<Tlabel, RegionQualityCriterion>::
merge_worst_regions_iteratively()
{
  if(m_verbosity) {
    std::clog << "yl::IterativeRegionMerger::merge_worst_regions_iteratively:\n"
              << "  computing initial region qualities..."
              << std::endl;
  }

  typedef typename RegionQualityCriterion::Cache CacheType;
  typedef RegionInQueue<Tlabel, CacheType> RegionType;
  typedef boost::heap::d_ary_heap<RegionType,
                                  boost::heap::arity<2>,
                                  boost::heap::mutable_<true> > RegionQueue;
  typedef typename RegionQueue::handle_type Handle;

  // Hold the labels to retrieve them in order of increasing region quality
  RegionQueue queue;
  std::map<Tlabel, Handle> label_to_handle;

  for(typename LabelVolume<Tlabel>::const_regions_iterator
        labels_it = m_label_volume.regions_begin(),
        labels_end = m_label_volume.regions_end();
      labels_it != labels_end;
      ++labels_it)
  {
    const Tlabel label = *labels_it;
    const CacheType cache
      = m_criterion.cache(m_label_volume, label);
    const float initial_quality = m_criterion.evaluate(cache);

    Handle handle = queue.push(RegionType(label, initial_quality, cache));
    (*handle).set_handle(handle);
    assert(label_to_handle.find(label) == label_to_handle.end());
    label_to_handle[label] = handle;
  }

  if(m_verbosity) {
    std::clog << "  " << queue.size() << " regions will be processed.\n"
              << "  filling in neighbourhoods..." << std::endl;
  }

  {
    const Tlabel background_label = m_label_volume.background_label();
    const int size_x = m_label_volume.volume().getSizeX();
    const int size_y = m_label_volume.volume().getSizeY();
    const int size_z = m_label_volume.volume().getSizeZ();
    for(int z = 0; z < size_z - 1; ++z)
    for(int y = 0; y < size_y - 1; ++y)
    for(int x = 0; x < size_x - 1; ++x)
    {
      const Tlabel self_label = m_label_volume.volume().at(x, y, z);

      // If the region was not included into the queue
      if(label_to_handle.count(self_label) == 0)
        continue;

      if(self_label != background_label) {
        Region<Tlabel>& self_region = *label_to_handle[self_label];
        const Tlabel xplus_label = m_label_volume.volume().at(x + 1, y, z);
        const Tlabel yplus_label = m_label_volume.volume().at(x, y + 1, z);
        const Tlabel zplus_label = m_label_volume.volume().at(x, y, z + 1);

        if(label_to_handle.count(xplus_label) != 0
           && xplus_label != background_label && self_label != xplus_label) {
          Region<Tlabel>& xplus_region = *label_to_handle[xplus_label];
          self_region.add_neighbour(xplus_region);
        }

        if(label_to_handle.count(yplus_label) != 0
           && yplus_label != background_label && self_label != yplus_label) {
          Region<Tlabel>& yplus_region = *label_to_handle[yplus_label];
          self_region.add_neighbour(yplus_region);
        }

        if(label_to_handle.count(zplus_label) != 0
           && zplus_label != background_label && self_label != zplus_label) {
          Region<Tlabel>& zplus_region = *label_to_handle[zplus_label];
          self_region.add_neighbour(zplus_region);
        }
      }
    }
  }

  unsigned int phase = 1;
  if(m_verbosity) {
    std::clog << "  iteratively merging regions..."
               "\n  Phase 1: processing non-traversing regions"
              << std::endl;
  }

  // Iteratively merge the worst region with one of its neighbours, until no
  // region can be improved further by merging a neighbour.
  while(!queue.empty())
  {
    const RegionType& worst_region = queue.top();
    const bool worst_region_is_traversing = worst_region.traversing();

    if(phase == 1 && worst_region_is_traversing) {
      phase = 2;
      if(m_verbosity)
        std::clog << "\n  Phase 2: all regions are traversing, "
                     "merging until goal diametre" << std::endl;
    }

    if(m_verbosity >= 2) {
      std::clog << "  ";
      if(RegionQueue::constant_time_size) {
        std::clog << queue.size() << " to go, ";
      }
      std::clog << m_label_volume.n_regions() << " regions, q = "
                << worst_region.quality() << "\r" << std::flush;
    }

    RegionType* best_neighbour_region_p = 0;
    CacheType best_cache;
    float best_quality;

    for(typename Region<Tlabel>::neighbour_iterator
          neighbour_it = worst_region.neighbours_begin(),
          neighbour_end = worst_region.neighbours_end();
        neighbour_it != neighbour_end;
        ++neighbour_it)
    {
      RegionType& neighbour_region = dynamic_cast<RegionType&>(*neighbour_it);
      const CacheType conjunction_cache
        = worst_region.cache() + neighbour_region.cache();
      const float conjunction_quality
        = m_criterion.evaluate_without_size_penalty(conjunction_cache);
      if(!best_neighbour_region_p || conjunction_quality > best_quality) {
        best_quality = conjunction_quality;
        best_cache = conjunction_cache;
        best_neighbour_region_p = &neighbour_region;
      }
    }

    if(!best_neighbour_region_p) {
      // The region has no neighbours, discard it
      if(m_verbosity >= 3) {
        std::clog << "\n    region " << worst_region
                  << " has no neighbours, discarding it." << std::endl;
      }
      queue.pop();
    } else if(!worst_region_is_traversing
              || best_quality > worst_region.quality()) {
      // The region is merged with its best candidate neighbour
      RegionType& best_neighbour_region = *best_neighbour_region_p;
      if(m_verbosity >= 4) {
        std::clog << "\n    merging with best neighbour " << best_neighbour_region
                  << " (q=" << best_quality << ")" << std::endl;
      }

      // worst_region is eaten by its best_neighbour_region
      m_label_volume.merge_regions(best_neighbour_region.label(),
                                   worst_region.label());

      best_neighbour_region.set_cache(best_cache);

      // Update new region's neighbourhood
      for(typename Region<Tlabel>::neighbour_iterator
            neighbour_it = worst_region.neighbours_begin(),
            neighbour_end = worst_region.neighbours_end();
          neighbour_it != neighbour_end;
          ++neighbour_it)
      {
        Region<Tlabel>& neighbour_region = *neighbour_it;
        if(neighbour_region != best_neighbour_region) {
          best_neighbour_region.add_neighbour(neighbour_region);
        }
      }

      // Get rid of worst_region. This needs to be done *before* updating the
      // heap!
      queue.pop();

      const float new_quality = m_criterion.evaluate(best_neighbour_region.cache());
      best_neighbour_region.update_quality(new_quality);
      queue.update(best_neighbour_region.handle());
    } else {
      // This case can only be reached when worst_region.traversing() is true
      // (see previous case). All regions are guaranteed to be traversing if
      // worst_region is so, because the ordering criterion puts all traversing
      // region above non-traversing regions. Thus, when all regions are
      // traversing then we can begin to drop regions (i.e. consider them
      // definitive). If we dropped regions before that, it could prevent
      // adjacent non-traversing regions from merging.
      assert(worst_region_is_traversing);
      if(m_verbosity >= 3) {
        std::clog << "\n    region " << worst_region
                  << " cannot be improved by merging a neighbour" << std::endl;
      }
      queue.pop();
    }
  }
  if(m_verbosity >= 1) {
    std::clog << "end: " << m_label_volume.n_regions() << " regions." << std::endl;
  }
}

};
