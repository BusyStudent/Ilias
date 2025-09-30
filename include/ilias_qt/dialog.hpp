#pragma once

#include <QDialog>
#include "object.hpp"

namespace ilias_qt {

/**
 * @brief Open the dialog and then async wait it finish
 * 
 * @param dialog The dialog reference, user must make sure, the lifetime of dialog live longer than the task
 * 
 * @return int, The result of the dialog
 */
inline auto execDialog(QDialog &dialog) -> ilias::Task<int> {
    dialog.open();
    auto [result] = (co_await QSignal(&dialog, &QDialog::finished)).value(); // The contract make the dialog live longer, so .value() directly
    co_return result;
}

// for impl auto val = co_await dialog;
inline auto toAwaitable(QDialog &dialog) -> ilias::Task<int> {
    return execDialog(dialog);
}

} // namespace ilias_qt

// for ADL
using ilias_qt::toAwaitable;