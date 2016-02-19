// Jubatus: Online machine learning framework for distributed environment
// Copyright (C) 2011 Preferred Networks and Nippon Telegraph and Telephone Corporation.
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License version 2.1 as published by the Free Software Foundation.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

#include "mecab_splitter.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>
#include "jubatus/core/fv_converter/exception.hpp"
#include "jubatus/core/fv_converter/util.hpp"

namespace jubatus {
namespace plugin {
namespace fv_converter {

using core::fv_converter::converter_exception;
using core::fv_converter::string_feature_element;

static MeCab::Model* create_mecab_model(const char* arg) {
  MeCab::Model* t = MeCab::createModel(arg);
  if (!t) {
    std::string msg("cannot make mecab tagger: ");
    msg += MeCab::getTaggerError();
    throw JUBATUS_EXCEPTION(converter_exception(msg));
  } else {
    return t;
  }
}

mecab_splitter::mecab_splitter()
    : model_(create_mecab_model("")),
      ngram_(1),
      base_(false) {
}

mecab_splitter::mecab_splitter(const char* arg, size_t ngram, bool base)
    : model_(create_mecab_model(arg)),
      ngram_(ngram),
      base_(base) {
  if (ngram == 0) {
    throw JUBATUS_EXCEPTION(
        converter_exception("ngram must be a positive number"));
  }
}

void mecab_splitter::extract(
    const std::string& string,
    std::vector<string_feature_element>& result) const {
  jubatus::util::lang::scoped_ptr<MeCab::Tagger> tagger(model_->createTagger());
  if (!tagger) {
    // cannot create tagger
    return;
  }
  jubatus::util::lang::scoped_ptr<MeCab::Lattice> lattice(
      model_->createLattice());
  if (!lattice) {
    // cannot create lattice
    return;
  }
  lattice->set_sentence(string.c_str());
  if (!tagger->parse(lattice.get())) {
    // parse error
    return;
  }

  const MeCab::Node* node = lattice->bos_node();
  size_t p = 0;

  // List of words and its bounds (start position and length of the surface.)
  // e.g., [ ("word1", (0, 5)), ("word2", (5, 5)), ... ]
  std::vector<std::pair<std::string, std::pair<size_t, size_t> > > words;
  for (; node; node = node->next) {
    if (node->stat == MECAB_BOS_NODE || node->stat == MECAB_EOS_NODE) {
      continue;
    }

    p += node->rlength - node->length;
    std::string word;
    if (base_) {
      // Use the base form of the word.
      // The 7th field of `node->feature` (CSV) contains the base form.
      std::string features = node->feature;
      size_t begin = 0, end = 0;
      for (size_t i = 0; i < 6; ++i) {
        begin = features.find(',', begin) + 1;
      }
      end = features.find(',', begin);
      word = std::string(features, begin, end - begin);

      if (word == "*") {
        // The base form is not available; use the surface.
        word = std::string(string, p, node->length);
      }
    } else {
      // Use surface (the original form of the word that appears in the
      // original sentence.)
      word = std::string(string, p, node->length);
    }
    words.push_back(std::make_pair(word, std::make_pair(p, node->length)));
    p += node->length;
  }

  // Need at least N words to extract N-gram.
  if (words.size() < ngram_) {
    return;
  }

  // Number of features: e.g., 2-gram for 4 words (bounds) like "a,b,c,d"
  // will produce 3 features ({"a,b", "b,c", "c,d"}.)
  size_t num_features = words.size() - ngram_ + 1;

  std::vector<string_feature_element> feature_elems;
  feature_elems.reserve(num_features);
  for (size_t i = 0; i < num_features; ++i) {
    std::string feature = words[i].first;
    size_t begin = words[i].second.first;
    size_t length = words[i].second.second;

    for (size_t j = 1; j < ngram_; ++j) {
      feature += "," + words[i+j].first;
      length += words[i+j].second.second;
    }

    feature_elems.push_back(
        string_feature_element(begin, length, feature, 1.0));
  }

  feature_elems.swap(result);
}

}  // namespace fv_converter
}  // namespace plugin
}  // namespace jubatus

extern "C" {
jubatus::plugin::fv_converter::mecab_splitter* create(
    const std::map<std::string, std::string>& params) {
  std::string param =
      jubatus::core::fv_converter::get_with_default(params, "arg", "");
  size_t ngram = jubatus::util::lang::lexical_cast<size_t>(
      jubatus::core::fv_converter::get_with_default(params, "ngram", "1"));
  std::string base_str = jubatus::core::fv_converter::get_with_default(
      params, "base", "false");
  if (base_str != "true" && base_str != "false") {
    throw JUBATUS_EXCEPTION(jubatus::core::fv_converter::converter_exception(
        "base must be a boolean value"));
  }
  bool base = (base_str == "true");

  return new jubatus::plugin::fv_converter::mecab_splitter(
      param.c_str(), ngram, base);
}

std::string version() {
  return JUBATUS_VERSION;
}
}  // extern "C"
