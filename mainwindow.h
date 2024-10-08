#ifndef MAINWINDOW_H
#define MAINWINDOW_H
#include "QueryClass.h"
#include "graphwidget.h"
#include "messagepopup.h"
#include <queue>
#include <QVBoxLayout>
#include <vector>

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void init(QueryC::QueryClass& qc); //initialize database
    void sortData(); //sorts the data so that it can be graphed
private slots:
    void showChart();
    void removeChart(GraphWidget *chart);
    void changeActivity();
    void disableButtons();
    void displayMessage();

private:
    Ui::MainWindow *ui;
    QueryC::QueryClass* database;
    std::vector<std::vector<QueryC::outPTime>> graphableData; //holds the sorted graphable data
    QWidget *centralWidget;
    QVBoxLayout *layout;
    int chartCounter;
};
#endif // MAINWINDOW_H
