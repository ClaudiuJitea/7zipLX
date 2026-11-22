#pragma once

#include <QString>
#include <QStringList>
#include <QList>

struct CExternalCodecs;

class SevenZipEngine
{
public:
  static SevenZipEngine &instance();

  bool initialize(QString *error = nullptr);
  void shutdown();

  QStringList archiveFormats() const;
  QStringList updateFormats() const;
  int formatIndex(const QString &name) const;
  QString formatName(int index) const;
  QString formatExtension(int index) const;
#ifdef Z7_EXTERNAL_CODECS
  const CExternalCodecs *externalCodecs() const;
  int externalCodecModuleCount() const;
#endif

private:
  SevenZipEngine() = default;
  bool m_initialized = false;
};
