#pragma once

#include <QFormLayout>

// Shared geometry for form-based dialogs.  Right-aligned labels and a growing
// field column keep labels visually attached to combo boxes, line edits and
// spin boxes instead of floating far to the left.
inline void configureDialogForm(QFormLayout *form, int horizontalSpacing = 16,
                                int verticalSpacing = 10)
{
  if (!form)
    return;
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  form->setRowWrapPolicy(QFormLayout::DontWrapRows);
  form->setHorizontalSpacing(horizontalSpacing);
  form->setVerticalSpacing(verticalSpacing);
  form->setContentsMargins(0, 0, 0, 0);
}
