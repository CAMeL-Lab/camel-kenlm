#include "lm/filter.hh"
#include "lm/phrase_substrings.hh"

#include <algorithm>
#include <functional>
#include <queue>
#include <vector>

namespace lm {
namespace detail { const StringPiece kEndSentence("</s>"); }
namespace {

typedef unsigned int Sentence;
typedef std::vector<Sentence> Sentences;

class Vertex;

class Arc {
  public:
    Arc() {}

    // For arcs from one vertex to another.  
    void SetPhrase(Vertex &from, Vertex &to, const Sentences &intersect) {
      Set(to, intersect);
      from_ = &from;
    }

    /* For arcs from before the n-gram begins to somewhere in the n-gram (right
     * aligned).  These have no from_ vertex; it implictly matches every
     * sentence.  This also handles when the n-gram is a substring of a phrase. 
     */
    void SetRight(Vertex &to, const Sentences &complete) {
      Set(to, complete);
      from_ = NULL;
    }

    Sentence Current() const {
      return *current_;
    }

    bool Empty() const {
      return current_ == last_;
    }

    /* When this function returns:
     * If Empty() then there's nothing left from this intersection.
     *
     * If Current() == to then to is part of the intersection. 
     *
     * Otherwise, Current() > to.  In this case, to is not part of the
     * intersection and neither is anything < Current().  To determine if
     * any value >= Current() is in the intersection, call LowerBound again
     * with the value.   
     */
    void LowerBound(const Sentence to);

  private:
    void Set(Vertex &to, const Sentences &sentences);

    const Sentence *current_;
    const Sentence *last_;
    Vertex *from_;
};

struct ArcLess : public std::binary_function<const Arc *, const Arc *, bool> {
  bool operator()(const Arc *first, const Arc *second) const {
    return first->Current() < second->Current();
  }
};

class Vertex {
  public:
    Vertex() : current_(0) {}

    Sentence Current() const {
      return current_;
    }

    bool Empty() const {
      return incoming_.empty();
    }

    // Precondition: !Empty()
    void LowerBound(const Sentence to) {
      assert(!Empty());
      // Union lower bound.  
      while (true) {
        Arc *top = incoming_.top();
        if (top->Current() > to) {
          current_ = top->Current();
          return;
        }
        // If top->Current() == to, we still need to verify that's an actual 
        // element and not just a bound.  
        incoming_.pop();
        top->LowerBound(to);
        if (!top->Empty()) {
          incoming_.push(top);
          if (top->Current() == to) {
            current_ = to;
            return;
          }
        } else if (Empty()) {
          return;
        }
      }
    }

  private:
    friend class Arc;

    void AddIncoming(Arc *arc) {
      if (!arc->Empty()) incoming_.push(arc);
    }

    unsigned int current_;
    std::priority_queue<Arc*, std::vector<Arc*>, ArcLess> incoming_;
};

void Arc::LowerBound(const Sentence to) {
  current_ = std::lower_bound(current_, last_, to);
  // If *current_ > to, don't advance from_.  The intervening values of
  // from_ may be useful for another one of its outgoing arcs.
  if (!from_ || Empty() || (Current() > to)) return;
  assert(Current() == to);
  if (from_->Empty()) {
    current_ = last_;
    return;
  }
  from_->LowerBound(to);
  if (from_->Empty()) {
    current_ = last_;
    return;
  }
  assert(from_->Current() >= to);
  if (from_->Current() > to) {
    current_ = std::lower_bound(current_ + 1, last_, from_->Current());
  }
}

void Arc::Set(Vertex &to, const Sentences &sentences) {
  current_ = &*sentences.begin();
  last_ = &*sentences.end();
  to.AddIncoming(this);
}


void BuildGraph(const PhraseSubstrings &phrase, const std::vector<size_t> &hashes, Vertex *const vertices, Arc *free_arc) {
  assert(!hashes.empty());

  const size_t *const first_word = &*hashes.begin();
  const size_t *const last_word = &*hashes.end() - 1;

  size_t hash = 0;
  const Sentences *found;
  // Phrases starting at or before the first word in the n-gram.
  {
    Vertex *vertex = vertices;
    for (const size_t *word = first_word; ; ++word, ++vertex) {
      boost::hash_combine(hash, *word);
      // Now hash is [hashes.begin(), word].
      if (word == last_word) {
        if (phrase.FindSubstring(hash, found))
          (free_arc++)->SetRight(*vertex, *found);
        break;
      }
      if (!phrase.FindRight(hash, found)) break;
      (free_arc++)->SetRight(*vertex, *found);
    }
  }

  // Phrases starting at the second or later word in the n-gram.   
  Vertex *vertex_from = vertices;
  for (const size_t *word_from = first_word + 1; word_from != &*hashes.end(); ++word_from, ++vertex_from) {
    hash = 0;
    Vertex *vertex_to = vertex_from + 1;
    for (const size_t *word_to = word_from; ; ++word_to, ++vertex_to) {
      // Notice that word_to and vertex_to have the same index.  
      boost::hash_combine(hash, *word_to);
      // Now hash covers [word_from, word_to].
      if (word_to == last_word) {
        if (phrase.FindLeft(hash, found))
          (free_arc++)->SetPhrase(*vertex_from, *vertex_to, *found);
        break;
      }
      if (!phrase.FindPhrase(hash, found)) break;
      (free_arc++)->SetPhrase(*vertex_from, *vertex_to, *found);
    }
  }
}

} // namespace

bool PhraseBinary::EvaluateUnion() {
  assert(!hashes_.empty());
  // Usually there are at most 6 words in an n-gram, so stack allocation is reasonable.  
  Vertex vertices[hashes_.size()];
  // One for every substring.  
  Arc arcs[((hashes_.size() + 1) * hashes_.size()) / 2];
  BuildGraph(substrings_, hashes_, vertices, arcs);
  Vertex &last_vertex = vertices[hashes_.size() - 1];

  if (last_vertex.Empty()) return false;
  unsigned int lower = 0;
  while (true) {
    last_vertex.LowerBound(lower);
    if (last_vertex.Empty()) return false;
    if (last_vertex.Current() == lower) return true;
    lower = last_vertex.Current();
  }
}

} // namespace lm