/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Feature processing for FFModel (feed-forward SmartSelection model).

#ifndef LIBTEXTCLASSIFIER_SMARTSELECT_FEATURE_PROCESSOR_H_
#define LIBTEXTCLASSIFIER_SMARTSELECT_FEATURE_PROCESSOR_H_

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "common/feature-extractor.h"
#include "smartselect/text-classification-model.pb.h"
#include "smartselect/token-feature-extractor.h"
#include "smartselect/tokenizer.h"
#include "smartselect/types.h"

namespace libtextclassifier {

constexpr int kInvalidLabel = -1;

namespace internal {

// Parses the serialized protocol buffer.
FeatureProcessorOptions ParseSerializedOptions(
    const std::string& serialized_options);

TokenFeatureExtractorOptions BuildTokenFeatureExtractorOptions(
    const FeatureProcessorOptions& options);

// Returns a modified version of the selection_with_context, such that only the
// line that contains the clicked span is kept, the number of codepoints
// the selection was moved by.
std::pair<SelectionWithContext, int> ExtractLineWithClick(
    const SelectionWithContext& selection_with_context);

// Splits tokens that contain the selection boundary inside them.
// E.g. "foo{bar}@google.com" -> "foo", "bar", "@google.com"
void SplitTokensOnSelectionBoundaries(CodepointSpan selection,
                                      std::vector<Token>* tokens);

}  // namespace internal

TokenSpan CodepointSpanToTokenSpan(const std::vector<Token>& selectable_tokens,
                                   CodepointSpan codepoint_span);

// Returns a modified version of the context string, such that only the
// line that contains the span is kept. Also returns a codepoint shift
// size that happend. If the span spans multiple lines, returns the original
// input with zero shift.
// The following characters are considered to be line separators: '\n', '|'
std::pair<std::string, int> ExtractLineWithSpan(const std::string& context,
                                                CodepointSpan span);

// Takes care of preparing features for the FFModel.
class FeatureProcessor {
 public:
  explicit FeatureProcessor(const FeatureProcessorOptions& options)
      : options_(options),
        feature_extractor_(
            internal::BuildTokenFeatureExtractorOptions(options)),
        feature_type_(FeatureProcessor::kFeatureTypeName,
                      options.num_buckets()),
        tokenizer_({options.tokenization_codepoint_config().begin(),
                    options.tokenization_codepoint_config().end()}),
        random_(new std::mt19937(std::random_device()())) {
    MakeLabelMaps();
  }

  explicit FeatureProcessor(const std::string& serialized_options)
      : FeatureProcessor(internal::ParseSerializedOptions(serialized_options)) {
  }

  CodepointSpan ClickRandomTokenInSelection(
      const SelectionWithContext& selection_with_context) const;

  // Tokenizes the input string using the selected tokenization method.
  std::vector<Token> Tokenize(const std::string& utf8_text) const;

  // NOTE: If dropout is on, subsequent calls of this function with the same
  // arguments might return different results.
  bool GetFeaturesAndLabels(const SelectionWithContext& selection_with_context,
                            std::vector<nlp_core::FeatureVector>* features,
                            std::vector<float>* extra_features,
                            std::vector<CodepointSpan>* selection_label_spans,
                            int* selection_label,
                            CodepointSpan* selection_codepoint_label,
                            int* classification_label) const;

  // Same as above but uses std::vector instead of FeatureVector.
  // NOTE: If dropout is on, subsequent calls of this function with the same
  // arguments might return different results.
  bool GetFeaturesAndLabels(
      const SelectionWithContext& selection_with_context,
      std::vector<std::vector<std::pair<int, float>>>* features,
      std::vector<float>* extra_features,
      std::vector<CodepointSpan>* selection_label_spans, int* selection_label,
      CodepointSpan* selection_codepoint_label,
      int* classification_label) const;

  // Converts a label into a token span.
  bool LabelToTokenSpan(int label, TokenSpan* token_span) const;

  // Gets the string value for given collection label.
  std::string LabelToCollection(int label) const;

  // Gets the total number of collections of the model.
  int NumCollections() const { return collection_to_label_.size(); }

  // Gets the name of the default collection.
  std::string GetDefaultCollection() const {
    return options_.collections(options_.default_collection());
  }

  FeatureProcessorOptions GetOptions() const { return options_; }

  int GetSelectionLabelCount() const { return label_to_selection_.size(); }

  // Sets the source of randomness.
  void SetRandom(std::mt19937* new_random) { random_.reset(new_random); }

 protected:
  // Extracts features for given word.
  std::vector<int> GetWordFeatures(const std::string& word) const;

  // NOTE: If dropout is on, subsequent calls of this function with the same
  // arguments might return different results.
  bool ComputeFeatures(int click_pos,
                       const std::vector<Token>& selectable_tokens,
                       CodepointSpan selected_span,
                       std::vector<nlp_core::FeatureVector>* features,
                       std::vector<float>* extra_features,
                       std::vector<Token>* output_tokens) const;

  // Helper function that computes how much left context and how much right
  // context should be dropped. Uses a mutable random_ member as a source of
  // randomness.
  bool GetContextDropoutRange(int* dropout_left, int* dropout_right) const;

  // Returns the class id corresponding to the given string collection
  // identifier. There is a catch-all class id that the function returns for
  // unknown collections.
  int CollectionToLabel(const std::string& collection) const;

  // Prepares mapping from collection names to labels.
  void MakeLabelMaps();

  // Gets the number of spannable tokens for the model.
  //
  // Spannable tokens are those tokens of context, which the model predicts
  // selection spans over (i.e., there is 1:1 correspondence between the output
  // classes of the model and each of the spannable tokens).
  int GetNumContextTokens() const { return options_.context_size() * 2 + 1; }

  // Converts a label into a span of codepoint indices corresponding to it
  // given output_tokens.
  bool LabelToSpan(int label, const std::vector<Token>& output_tokens,
                   CodepointSpan* span) const;

  // Converts a span to the corresponding label given output_tokens.
  bool SpanToLabel(const std::pair<CodepointIndex, CodepointIndex>& span,
                   const std::vector<Token>& output_tokens, int* label) const;

  // Converts a token span to the corresponding label.
  int TokenSpanToLabel(const std::pair<TokenIndex, TokenIndex>& span) const;

 private:
  FeatureProcessorOptions options_;

  TokenFeatureExtractor feature_extractor_;

  static const char* const kFeatureTypeName;

  nlp_core::NumericFeatureType feature_type_;

  // Mapping between token selection spans and labels ids.
  std::map<TokenSpan, int> selection_to_label_;
  std::vector<TokenSpan> label_to_selection_;

  // Mapping between collections and labels.
  std::map<std::string, int> collection_to_label_;

  Tokenizer tokenizer_;

  // Source of randomness.
  mutable std::unique_ptr<std::mt19937> random_;
};

}  // namespace libtextclassifier

#endif  // LIBTEXTCLASSIFIER_SMARTSELECT_FEATURE_PROCESSOR_H_