#pragma once

#include "backbook/domain/state.hpp"
#include "backbook/journal/command_batch.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace backbook::test_support {

template <typename IdType>
[[nodiscard]] inline IdType id(const std::string_view text) {
    auto parsed = IdType::parse(text);
    EXPECT_TRUE(parsed);
    return std::move(parsed).value();
}

[[nodiscard]] inline domain::IsoDate date(const std::string_view text) {
    auto parsed = domain::IsoDate::parse(text);
    EXPECT_TRUE(parsed);
    return std::move(parsed).value();
}

[[nodiscard]] inline domain::Money money(const domain::Currency currency,
                                         const std::int64_t minor_units) {
    auto created = domain::Money::from_minor_units(currency, minor_units);
    EXPECT_TRUE(created);
    return std::move(created).value();
}

[[nodiscard]] inline domain::FxTerms
terms(const std::int64_t pay_minor_units = 10'000'000,
      const std::int64_t receive_minor_units = 1'500'000'000) {
    auto created = domain::FxTerms::create(
        domain::InstrumentKind::FxSpot,
        date("2026-07-25"),
        date("2026-07-27"),
        money(domain::Currency::Usd, pay_minor_units),
        money(domain::Currency::Jpy, receive_minor_units));
    EXPECT_TRUE(created);
    return std::move(created).value();
}

[[nodiscard]] inline domain::LimitPath limit_path() {
    return domain::LimitPath(id<domain::CounterpartyId>("CPTY-A"),
                             id<domain::NettingSetId>("NET-A"),
                             id<domain::BookId>("BOOK-FX-1"));
}

[[nodiscard]] inline domain::LimitHierarchy limits() {
    std::vector<domain::LimitDefinition> definitions;
    const auto path = limit_path();
    for (const domain::Currency currency : {domain::Currency::Usd,
                                            domain::Currency::Jpy,
                                            domain::Currency::Kwd}) {
        const std::int64_t capacity =
            currency == domain::Currency::Usd ? 15'000'000 : 2'000'000'000;
        for (const auto& node : path.nodes()) {
            definitions.push_back(
                domain::LimitDefinition{node, money(currency, capacity)});
        }
    }
    auto created = domain::LimitHierarchy::create(std::move(definitions));
    EXPECT_TRUE(created);
    return std::move(created).value();
}

[[nodiscard]] inline domain::ConfirmationPostingIds
confirmation_ids(const std::string& prefix) {
    return domain::ConfirmationPostingIds{
        id<domain::PostingId>(prefix + "-PAY-CONTROL-D"),
        id<domain::PostingId>(prefix + "-PAY-PAYABLE-C"),
        id<domain::PostingId>(prefix + "-RECV-RECEIVABLE-D"),
        id<domain::PostingId>(prefix + "-RECV-CONTROL-C")};
}

[[nodiscard]] inline domain::ReversalPostingIds
reversal_ids(const std::string& prefix) {
    return domain::ReversalPostingIds{
        id<domain::PostingId>(prefix + "-PAY-CONTROL-C"),
        id<domain::PostingId>(prefix + "-PAY-PAYABLE-D"),
        id<domain::PostingId>(prefix + "-RECV-RECEIVABLE-C"),
        id<domain::PostingId>(prefix + "-RECV-CONTROL-D")};
}

[[nodiscard]] inline journal::CommandBatch batch(const std::uint64_t sequence,
                                                 journal::Event event,
                                                 journal::CommandResult result,
                                                 journal::Bytes request = {
                                                     0x10U, 0x20U, 0x30U}) {
    auto created = journal::CommandBatch::create(
        sequence,
        id<domain::CommandId>("CMD-" + std::to_string(sequence)),
        std::move(request),
        std::vector<journal::Event>{std::move(event)},
        std::move(result));
    EXPECT_TRUE(created);
    return std::move(created).value();
}

[[nodiscard]] inline std::vector<journal::CommandBatch>
canonical_batches(const bool include_no_op_eod = false) {
    const auto trade_id = id<domain::TradeId>("TRD-1001");
    const auto path = limit_path();
    const auto first_terms = terms();
    const auto replacement_terms = terms(10'125'000, 1'518'750'000);

    std::vector<journal::CommandBatch> batches;
    batches.push_back(batch(1U,
                            journal::TradeBookedEvent{trade_id,
                                                      path.book_id(),
                                                      path.counterparty_id(),
                                                      path.netting_set_id(),
                                                      first_terms},
                            journal::TradeBookedResult{trade_id, 1U, 1U}));
    batches.push_back(batch(
        2U,
        journal::TradeConfirmedEvent{trade_id, 1U, confirmation_ids("PST-V1")},
        journal::TradeConfirmedResult{trade_id, 1U, 2U}));
    batches.push_back(
        batch(3U,
              journal::TradeAmendedEvent{trade_id,
                                         1U,
                                         replacement_terms,
                                         reversal_ids("PST-V1-REV"),
                                         confirmation_ids("PST-V2")},
              journal::TradeAmendedResult{trade_id, 1U, 2U, 3U}));
    batches.push_back(batch(4U,
                            journal::EodRunEvent{date("2026-07-27")},
                            journal::EodRunResult{date("2026-07-27"), 1U, 4U}));

    if (include_no_op_eod) {
        batches.push_back(
            batch(5U,
                  journal::EodRunEvent{date("2026-07-28")},
                  journal::EodRunResult{date("2026-07-28"), 0U, 4U}));
    }
    return batches;
}

}  // namespace backbook::test_support
