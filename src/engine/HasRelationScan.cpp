// Copyright 2018, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: Florian Kramer (florian.kramer@mail.uni-freiburg.de)

#include "HasRelationScan.h"

template <typename A, typename R>
void doComputeSubqueryS(const std::vector<A>* input,
                        const size_t inputSubjectColumn, std::vector<R>* result,
                        const std::vector<PatternID>& hasPattern,
                        const CompactStringVector<Id, Id>& hasRelation,
                        const CompactStringVector<size_t, Id>& patterns);

HasRelationScan::HasRelationScan(QueryExecutionContext* qec, ScanType type)
    : Operation(qec),
      _type(type),
      _subtree(nullptr),
      _subtreeColIndex(-1),
      _subject(),
      _object() {}

string HasRelationScan::asString(size_t indent) const {
  std::ostringstream os;
  for (size_t i = 0; i < indent; ++i) {
    os << " ";
  }
  switch (_type) {
    case ScanType::FREE_S:
      os << "HAS_RELATION_SCAN with O = " << _object;
      break;
    case ScanType::FREE_O:
      os << "HAS_RELATION_SCAN with S = " << _subject;
      break;
    case ScanType::FULL_SCAN:
      os << "HAS_RELATION_SCAN for the full relation";
      break;
    case ScanType::SUBQUERY_S:
      os << "HAS_RELATION_SCAN with S = " << _subtree->asString(indent);
      break;
  }
  return os.str();
}

size_t HasRelationScan::getResultWidth() const {
  switch (_type) {
    case ScanType::FREE_S:
      return 1;
    case ScanType::FREE_O:
      return 1;
    case ScanType::FULL_SCAN:
      return 2;
    case ScanType::SUBQUERY_S:
      return _subtree->getResultWidth() + 1;
  }
  return -1;
}

size_t HasRelationScan::resultSortedOn() const {
  switch (_type) {
    case ScanType::FREE_S:
      // is the lack of sorting here a problem?
      return -1;
    case ScanType::FREE_O:
      return 0;
    case ScanType::FULL_SCAN:
      return 0;
    case ScanType::SUBQUERY_S:
      return _subtree->resultSortedOn();
  }
  return -1;
}

std::unordered_map<string, size_t> HasRelationScan::getVariableColumns() const {
  std::unordered_map<string, size_t> varCols;
  switch (_type) {
    case ScanType::FREE_S:
      varCols.insert(std::make_pair(_subject, 0));
      break;
    case ScanType::FREE_O:
      varCols.insert(std::make_pair(_object, 0));
      break;
    case ScanType::FULL_SCAN:
      varCols.insert(std::make_pair(_subject, 0));
      varCols.insert(std::make_pair(_object, 1));
      break;
    case ScanType::SUBQUERY_S:
      varCols = _subtree->getVariableColumnMap();
      varCols.insert(std::make_pair(_object, getResultWidth() - 1));
      break;
  }
  return varCols;
}

void HasRelationScan::setTextLimit(size_t limit) {
  if (_type == ScanType::SUBQUERY_S) {
    _subtree->setTextLimit(limit);
  }
}

bool HasRelationScan::knownEmptyResult() {
  if (_type == ScanType::SUBQUERY_S) {
    return _subtree->knownEmptyResult();
  } else {
    return false;
  }
}

float HasRelationScan::getMultiplicity(size_t col) {
  switch (_type) {
    case ScanType::FREE_S:
      if (col == 0) {
        return getIndex().getHasRelationMultiplicityEntities();
      }
      break;
    case ScanType::FREE_O:
      if (col == 0) {
        return getIndex().getHasRelationMultiplicityPredicates();
      }
      break;
    case ScanType::FULL_SCAN:
      if (col == 0) {
        return getIndex().getHasRelationMultiplicityEntities();
      } else if (col == 1) {
        return getIndex().getHasRelationMultiplicityPredicates();
      }
      break;
    case ScanType::SUBQUERY_S:
      if (col < getResultWidth() - 1) {
        return _subtree->getMultiplicity(col) *
               getIndex().getHasRelationMultiplicityPredicates();
      } else {
        return _subtree->getMultiplicity(_subtreeColIndex) *
               getIndex().getHasRelationMultiplicityPredicates();
      }
      break;
  }
  return 1;
}

size_t HasRelationScan::getSizeEstimate() {
  switch (_type) {
    case ScanType::FREE_S:
      return static_cast<size_t>(
          getIndex().getHasRelationMultiplicityEntities());
    case ScanType::FREE_O:
      return static_cast<size_t>(
          getIndex().getHasRelationMultiplicityPredicates());
    case ScanType::FULL_SCAN:
      return getIndex().getHasRelationFullSize();
    case ScanType::SUBQUERY_S:

      size_t nofDistinctLeft = std::max(
          size_t(1),
          static_cast<size_t>(_subtree->getSizeEstimate() /
                              _subtree->getMultiplicity(_subtreeColIndex)));
      size_t nofDistinctRight = std::max(
          size_t(1), static_cast<size_t>(
                         getIndex().getHasRelationFullSize() /
                         getIndex().getHasRelationMultiplicityPredicates()));
      size_t nofDistinctInResult = std::min(nofDistinctLeft, nofDistinctRight);

      double jcMultiplicityInResult =
          _subtree->getMultiplicity(_subtreeColIndex) *
          getIndex().getHasRelationMultiplicityPredicates();
      return std::max(size_t(1), static_cast<size_t>(jcMultiplicityInResult *
                                                     nofDistinctInResult));
  }
  return 0;
}

size_t HasRelationScan::getCostEstimate() {
  // TODO: these size estimates only work if all predicates are functional
  switch (_type) {
    case ScanType::FREE_S:
      return getSizeEstimate();
    case ScanType::FREE_O:
      return getSizeEstimate();
    case ScanType::FULL_SCAN:
      return getSizeEstimate();
    case ScanType::SUBQUERY_S:
      return _subtree->getCostEstimate() + getSizeEstimate();
  }
  return 0;
}

void HasRelationScan::computeResult(ResultTable* result) const {
  result->_nofColumns = getResultWidth();
  result->_sortedBy = resultSortedOn();

  const std::vector<PatternID>& hasPattern = getIndex().getHasPattern();
  const CompactStringVector<Id, Id>& hasRelation = getIndex().getHasRelation();
  const CompactStringVector<size_t, Id>& patterns = getIndex().getPatterns();

  switch (_type) {
    case ScanType::FREE_S: {
      Id objectId;
      if (!getIndex().getVocab().getId(_object, &objectId)) {
        AD_THROW(ad_semsearch::Exception::BAD_INPUT,
                 "The predicate '" + _object + "' is not in the vocabulary.");
      }
      HasRelationScan::computeFreeS(result, objectId, hasPattern, hasRelation,
                                    patterns);
    } break;
    case ScanType::FREE_O: {
      Id subjectId;
      if (!getIndex().getVocab().getId(_subject, &subjectId)) {
        AD_THROW(ad_semsearch::Exception::BAD_INPUT,
                 "The subject " + _subject + " is not in the vocabulary.");
      }
      HasRelationScan::computeFreeO(result, subjectId, hasPattern, hasRelation,
                                    patterns);
    } break;
    case ScanType::FULL_SCAN:
      HasRelationScan::computeFullScan(result, hasPattern, hasRelation,
                                       patterns,
                                       getIndex().getHasRelationFullSize());
      break;
    case ScanType::SUBQUERY_S:
      HasRelationScan::computeSubqueryS(result, _subtree, _subtreeColIndex,
                                        hasPattern, hasRelation, patterns);
      break;
  }
  result->finish();
}

void HasRelationScan::computeFreeS(
    ResultTable* result, size_t objectId,
    const std::vector<PatternID>& hasPattern,
    const CompactStringVector<Id, Id>& hasRelation,
    const CompactStringVector<size_t, Id>& patterns) {
  result->_resultTypes.push_back(ResultTable::ResultType::KB);
  std::vector<std::array<Id, 1>>* fixedSizeData =
      new std::vector<std::array<Id, 1>>();
  result->_fixedSizeData = fixedSizeData;

  Id id = 0;
  while (id < hasPattern.size() || id < hasRelation.size()) {
    if (id < hasPattern.size() && hasPattern[id] != NO_PATTERN) {
      // add the pattern
      size_t numPredicates;
      Id* patternData;
      std::tie(patternData, numPredicates) = patterns[hasPattern[id]];
      for (size_t i = 0; i < numPredicates; i++) {
        if (patternData[i] == objectId) {
          fixedSizeData->push_back({{id}});
        }
      }
    } else if (id < hasRelation.size()) {
      // add the relations
      size_t numPredicates;
      Id* predicateData;
      std::tie(predicateData, numPredicates) = hasRelation[id];
      for (size_t i = 0; i < numPredicates; i++) {
        if (predicateData[i] == objectId) {
          fixedSizeData->push_back({{id}});
        }
      }
    }
    id++;
  }
}

void HasRelationScan::computeFreeO(
    ResultTable* result, size_t subjectId,
    const std::vector<PatternID>& hasPattern,
    const CompactStringVector<Id, Id>& hasRelation,
    const CompactStringVector<size_t, Id>& patterns) {
  result->_resultTypes.push_back(ResultTable::ResultType::KB);
  std::vector<std::array<Id, 1>>* fixedSizeData =
      new std::vector<std::array<Id, 1>>();
  result->_fixedSizeData = fixedSizeData;

  if (subjectId < hasPattern.size() && hasPattern[subjectId] != NO_PATTERN) {
    // add the pattern
    size_t numPredicates;
    Id* patternData;
    std::tie(patternData, numPredicates) = patterns[hasPattern[subjectId]];
    for (size_t i = 0; i < numPredicates; i++) {
      fixedSizeData->push_back({{patternData[i]}});
    }
  } else if (subjectId < hasRelation.size()) {
    // add the relations
    size_t numPredicates;
    Id* predicateData;
    std::tie(predicateData, numPredicates) = hasRelation[subjectId];
    for (size_t i = 0; i < numPredicates; i++) {
      fixedSizeData->push_back({{predicateData[i]}});
    }
  }
}

void HasRelationScan::computeFullScan(
    ResultTable* result, const std::vector<PatternID>& hasPattern,
    const CompactStringVector<Id, Id>& hasRelation,
    const CompactStringVector<size_t, Id>& patterns, size_t resultSize) {
  result->_resultTypes.push_back(ResultTable::ResultType::KB);
  result->_resultTypes.push_back(ResultTable::ResultType::KB);
  std::vector<std::array<Id, 2>>* fixedSizeData =
      new std::vector<std::array<Id, 2>>();
  result->_fixedSizeData = fixedSizeData;
  fixedSizeData->reserve(resultSize);

  size_t id = 0;
  while (id < hasPattern.size() || id < hasRelation.size()) {
    if (id < hasPattern.size() && hasPattern[id] != NO_PATTERN) {
      // add the pattern
      size_t numPredicates;
      Id* patternData;
      std::tie(patternData, numPredicates) = patterns[hasPattern[id]];
      for (size_t i = 0; i < numPredicates; i++) {
        fixedSizeData->push_back({{id, patternData[i]}});
      }
    } else if (id < hasRelation.size()) {
      // add the relations
      size_t numPredicates;
      Id* predicateData;
      std::tie(predicateData, numPredicates) = hasRelation[id];
      for (size_t i = 0; i < numPredicates; i++) {
        fixedSizeData->push_back({{id, predicateData[i]}});
      }
    }
    id++;
  }
}

/**
 * @brief This struct resizes std::containers if they are vectors,
 *        allowing for nicer templated code that can handle both result vectors
 *        of vectors and vectors of arrays.
 */
template <typename T, typename C>
struct resizeIfVec {
  static void resize(T& t, int size) {
    (void)t;
    (void)size;
  }
};

template <typename C>
struct resizeIfVec<vector<C>, C> {
  static void resize(vector<C>& t, int size) { t.resize(size); }
};

template <typename A, typename R>
void doComputeSubqueryS(const std::vector<A>* input,
                        const size_t inputSubjectColumn, std::vector<R>* result,
                        const std::vector<PatternID>& hasPattern,
                        const CompactStringVector<Id, Id>& hasRelation,
                        const CompactStringVector<size_t, Id>& patterns) {
  for (size_t i = 0; i < input->size(); i++) {
    const A& inputRow = (*input)[i];
    size_t id = inputRow[inputSubjectColumn];
    if (id < hasPattern.size() && hasPattern[id] != NO_PATTERN) {
      // add the pattern
      size_t numPredicates;
      Id* patternData;
      std::tie(patternData, numPredicates) = patterns[hasPattern[id]];
      for (size_t i = 0; i < numPredicates; i++) {
        R row;
        resizeIfVec<R, Id>::resize(row, inputRow.size() + 1);
        for (size_t j = 0; j < inputRow.size(); j++) {
          row[j] = inputRow[j];
        }
        row[inputRow.size()] = patternData[i];
        result->push_back(row);
      }
    } else if (id < hasRelation.size()) {
      // add the relations
      size_t numPredicates;
      Id* predicateData;
      std::tie(predicateData, numPredicates) = hasRelation[id];
      for (size_t i = 0; i < numPredicates; i++) {
        R row;
        resizeIfVec<R, Id>::resize(row, inputRow.size() + 1);
        for (size_t j = 0; j < inputRow.size(); j++) {
          row[j] = inputRow[j];
        }
        row[inputRow.size()] = predicateData[i];
        result->push_back(row);
      }
    } else {
      break;
    }
  }
}

/**
 * @brief  This struct is used to call doComputeSubqueryS with the correct
 * template parameters. Using it is equivalent to a structure of nested if
 * clauses, checking if the inputColCount and resultColCount are of a certain
 * value, and then calling doGroupBy.
 */
template <int InputColCount>
struct CallComputeSubqueryS {
  static void call(int inputColCount, const ResultTable* input,
                   const size_t inputSubjectColumn, ResultTable* result,
                   const std::vector<PatternID>& hasPattern,
                   const CompactStringVector<Id, Id>& hasRelation,
                   const CompactStringVector<size_t, Id>& patterns) {
    if (InputColCount == inputColCount) {
      if (InputColCount < 5) {
        // Both the input and the result use _fixedSizeData
        result->_fixedSizeData = new vector<array<Id, InputColCount + 1>>();
        doComputeSubqueryS<std::array<Id, InputColCount>,
                           std::array<Id, InputColCount + 1>>(
            static_cast<vector<array<Id, InputColCount>>*>(
                input->_fixedSizeData),
            inputSubjectColumn,
            static_cast<vector<array<Id, InputColCount + 1>>*>(
                result->_fixedSizeData),
            hasPattern, hasRelation, patterns);
      } else {
        // The input uses _fixedSizeData, but the output uses _varSizeData
        doComputeSubqueryS<std::array<Id, InputColCount>, std::vector<Id>>(
            static_cast<vector<array<Id, InputColCount>>*>(
                input->_fixedSizeData),
            inputSubjectColumn, &result->_varSizeData, hasPattern, hasRelation,
            patterns);
      }
    } else {
      CallComputeSubqueryS<InputColCount + 1>::call(
          inputColCount, input, inputSubjectColumn, result, hasPattern,
          hasRelation, patterns);
    }
  }
};

template <>
struct CallComputeSubqueryS<6> {
  static void call(int inputColCount, const ResultTable* input,
                   const size_t inputSubjectColumn, ResultTable* result,
                   const std::vector<PatternID>& hasPattern,
                   const CompactStringVector<Id, Id>& hasRelation,
                   const CompactStringVector<size_t, Id>& patterns) {
    // avoid unused warnings from the compiler
    (void)inputColCount;
    // Both the input and the result use _varSizeData
    doComputeSubqueryS<std::vector<Id>, std::vector<Id>>(
        &input->_varSizeData, inputSubjectColumn, &result->_varSizeData,
        hasPattern, hasRelation, patterns);
  }
};

void HasRelationScan::computeSubqueryS(
    ResultTable* result, const std::shared_ptr<QueryExecutionTree> subtree,
    const size_t subtreeColIndex, const std::vector<PatternID>& hasPattern,
    const CompactStringVector<Id, Id>& hasRelation,
    const CompactStringVector<size_t, Id>& patterns) {
  std::shared_ptr<const ResultTable> subresult = subtree->getResult();

  result->_resultTypes.insert(result->_resultTypes.begin(),
                              subresult->_resultTypes.begin(),
                              subresult->_resultTypes.end());
  result->_resultTypes.push_back(ResultTable::ResultType::KB);

  CallComputeSubqueryS<1>::call(subresult->_nofColumns, subresult.get(),
                                subtreeColIndex, result, hasPattern,
                                hasRelation, patterns);
}

void HasRelationScan::setSubject(const std::string& subject) {
  _subject = subject;
}

void HasRelationScan::setObject(const std::string& object) { _object = object; }

const std::string& HasRelationScan::getObject() const { return _object; }

void HasRelationScan::setSubtree(std::shared_ptr<QueryExecutionTree> subtree) {
  _subtree = subtree;
}

void HasRelationScan::setSubtreeSubjectColumn(size_t colIndex) {
  _subtreeColIndex = colIndex;
}

HasRelationScan::ScanType HasRelationScan::getType() const { return _type; }
