// This is the source code of LuxuryGram for Desktop.
// See LEGAL for license and copyright information.
#pragma once

namespace LuxuryStartup {

class Splash final : public QWidget {
public:
	Splash();

	void setStage(const QString &text, float64 progress);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	QPixmap _logo;
	QString _version;
	QString _stage;
	float64 _progress = 0.;

};

[[nodiscard]] std::unique_ptr<Splash> ShowSplash();

} // namespace LuxuryStartup
