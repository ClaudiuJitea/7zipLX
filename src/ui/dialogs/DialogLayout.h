#pragma once

#include <QFormLayout>
#include <QLabel>
#include <QLayout>
#include <QWidget>

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

// Call after all rows were added.  Gives every row label the height of its
// field and centers the text in it, so the label's baseline lines up with the
// text inside the line edit, combo box, spin box or progress bar next to it.
inline void alignDialogFormLabels(QFormLayout *form)
{
  if (!form)
    return;
  for (int row = 0; row < form->rowCount(); ++row)
  {
    QLayoutItem *labelItem = form->itemAt(row, QFormLayout::LabelRole);
    QLayoutItem *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
    if (!labelItem || !fieldItem)
      continue;
    auto *label = qobject_cast<QLabel *>(labelItem->widget());
    if (!label)
      continue;
    const int fieldHeight = fieldItem->sizeHint().height();
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    label->setProperty("formLabel", true);
    if (fieldHeight > label->sizeHint().height())
      label->setMinimumHeight(fieldHeight);
  }
}
