// SmartOneHandWriter - banco di prova Qt del CORE "nuova concezione".
//
// Quattro campi (come da specifica) + toggle T9/Classico:
//   1) Contesto, con un marcatore dello slot parola (▮). Placeholder esplicativo.
//   2) Parola codificata (simboli T9 in modalita' T9, lettere reali in Classico).
//   3) Parole decodificate (match ordinati per contesto).
//   4) Parole predette (ventaglio di parola successiva del match in cima).
// Aggiornamento live a ogni modifica. Linka direttamente sohw_core (C++).
#include "sohw/core.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <fstream>
#include <string>

namespace {

// Il marcatore dello slot: dove si trova la parola da decodificare nel contesto.
const QString kSlot = QString::fromUtf8("\xE2\x96\xAE");  // ▮ U+25AE

std::string dataPath(const char* name) {
#ifdef SOHW_DATA_DIR
    return std::string(SOHW_DATA_DIR) + "/" + name;
#else
    return std::string("data/") + name;
#endif
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // --- CORE ---------------------------------------------------------------
    sohw::Core core;
    {
        std::ifstream wl(dataPath("wordlist_it.txt"), std::ios::binary);
        if (wl) core.loadWordlist(wl);
    }
    core.loadBigramModel(dataPath("it.bigrams.bin"));   // no-op se assente

    // --- UI -----------------------------------------------------------------
    auto* w = new QWidget;
    w->setWindowTitle(QString::fromUtf8("SmartOneHandWriter — banco CORE"));
    auto* root = new QVBoxLayout(w);

    auto* modeBox = new QComboBox;
    modeBox->addItem("T9 (matching attivo)");
    modeBox->addItem(QString::fromUtf8("Classico (digitazione reale)"));

    auto* ctxEdit = new QLineEdit;
    ctxEdit->setPlaceholderText(
        QString::fromUtf8("Contesto con ▮ dove va la parola — es: \"per ▮ strada\""));
    ctxEdit->setText(QString::fromUtf8("per ▮"));

    auto* encEdit = new QLineEdit;
    encEdit->setPlaceholderText(QString::fromUtf8("Parola codificata (T9, es: 52) o lettere"));

    auto* form = new QFormLayout;
    form->addRow("Modalità", modeBox);
    form->addRow("Contesto", ctxEdit);
    form->addRow("Codificata", encEdit);
    root->addLayout(form);

    auto* matchOut = new QPlainTextEdit; matchOut->setReadOnly(true);
    auto* nextOut  = new QPlainTextEdit; nextOut->setReadOnly(true);

    auto* g1 = new QGroupBox("Parole decodificate (match)"); auto* l1 = new QVBoxLayout(g1); l1->addWidget(matchOut);
    auto* g2 = new QGroupBox("Parole predette (next-word del match in cima)"); auto* l2 = new QVBoxLayout(g2); l2->addWidget(nextOut);
    root->addWidget(g1);
    root->addWidget(g2);

    // --- ricalcolo live -----------------------------------------------------
    auto recompute = [&]() {
        core.setMode(modeBox->currentIndex() == 1 ? sohw::InputMode::Literal
                                                   : sohw::InputMode::T9);
        // Split del contesto sul marcatore dello slot.
        const QString text = ctxEdit->text();
        int slot = text.indexOf(kSlot);
        QString left = (slot < 0) ? text : text.left(slot);
        QString right = (slot < 0) ? QString() : text.mid(slot + kSlot.size());

        sohw::Context ctx{ left.toUtf8().constData(), right.toUtf8().constData() };
        sohw::CoreResult r = core.process(ctx, encEdit->text().toUtf8().constData(), 8, 6);

        QString m;
        for (const auto& s : r.matches)
            m += QString::fromUtf8(s.word.c_str()) +
                 QString("\t%1\n").arg(s.score, 0, 'f', 4);
        matchOut->setPlainText(m);

        QString n;
        if (!r.nextByMatch.empty() && !r.matches.empty()) {
            n += QString::fromUtf8("dopo \"%1\":\n").arg(QString::fromUtf8(r.matches[0].word.c_str()));
            for (const auto& s : r.nextByMatch[0])
                n += QString::fromUtf8(s.word.c_str()) +
                     QString("\t%1\n").arg(s.score, 0, 'f', 4);
        }
        nextOut->setPlainText(n);
    };

    QObject::connect(ctxEdit, &QLineEdit::textChanged, recompute);
    QObject::connect(encEdit, &QLineEdit::textChanged, recompute);
    QObject::connect(modeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), recompute);
    recompute();

    w->resize(560, 480);
    w->show();
    return app.exec();
}
