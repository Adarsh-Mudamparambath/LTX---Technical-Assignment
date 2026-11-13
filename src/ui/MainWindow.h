#pragma once

#include <QMainWindow>

#include <memory>

class FlightTableModel;
class QCheckBox;
class Globe3DWidget;
class Map2DWidget;
class MBTilesReader;
class QPushButton;
class QSplitter;
class QTableView;

/// The button row + table from the mock-up on the Instructions sheet.
/// The 2D map and 3D globe panes (Tasks 5-8) slot in underneath later.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// Tears the GL widgets down while m_tiles is still alive - Qt would
    /// otherwise destroy child widgets after this object's members are gone.
    ~MainWindow() override;

    /// Load a CSV without going through the file dialog (used at startup when
    /// NZ5_Flightdata.csv sits next to the executable).
    bool loadCsv(const QString& path);

    /// Point the 2D map and the 3D globe at an MBTiles archive (Task-5).
    bool loadTiles(const QString& path);

protected:
    /// Keeps table / map / globe at fixed proportions as the window resizes.
    /// QSplitter otherwise lets the table's fitted column widths crowd the two
    /// GL panes out, especially when the window is maximised. Once the user
    /// drags a handle their choice sticks.
    void resizeEvent(QResizeEvent* e) override;

private slots:
    void onLoadCsv();      ///< Task-1
    void onSort();         ///< Task-2
    void onWriteGeoJson(); ///< Task-3
    void onWriteBinary();  ///< Task-4

    /// Task-6: mirror one view's camera onto the other.
    void onMapViewChanged(double latitude, double longitude, double zoom);
    void onGlobeViewChanged(double latitude, double longitude, double zoom);

private:
    void updateButtonState();

    /// Status-bar readout of where the views are currently centred.
    void showViewPosition(double latitude, double longitude, double zoom);

    /// Debug hook behind FLIGHTVIZ_SYNCTEST: drives both views with
    /// synthesized mouse events and reports whether they stayed in step.
    void runSyncTest(const QString& reportPath);

    FlightTableModel* m_model       = nullptr;
    QTableView*       m_table       = nullptr;
    QPushButton*      m_loadButton  = nullptr;
    QPushButton*      m_sortButton  = nullptr;
    QPushButton*      m_jsonButton  = nullptr;
    QPushButton*      m_binaryButton = nullptr;
    QCheckBox*        m_syncBox      = nullptr;   ///< Task-6 on/off

    std::unique_ptr<MBTilesReader> m_tiles;   ///< outlives both GL widgets
    Map2DWidget*                   m_map2d  = nullptr;
    Globe3DWidget*                 m_globe  = nullptr;
    QSplitter*                     m_splitter      = nullptr;
    bool                           m_userSizedPanes = false;

    QString m_lastDirectory;
};
