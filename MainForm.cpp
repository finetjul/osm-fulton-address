/*
 * File:   mainForm.cpp
 * Author: saikrishna
 *
 * Created on January 1, 2014, 9:14 AM
 */

#include "MainForm.h"
#include "QFileDialog"
#include "QUrl"
#include "QXmlStreamReader"
#include "QDebug"
#include "qmath.h"
#include "Address.h"
#include <geos/algorithm/Angle.h>
#include <geos/geom/PrecisionModel.h>
#include <geos/geom/CoordinateSequenceFactory.h>
#include <geos/geom/CoordinateArraySequenceFactory.h>
#include <geos/geom/prep/PreparedPolygon.h>

MainForm::MainForm() {
    widget.setupUi(this);

    widget.doubleSpinBox->setValue(33.7857);
    widget.doubleSpinBox_2->setValue(-84.3982);
    widget.doubleSpinBox_3->setValue(33.7633);
    widget.doubleSpinBox_4->setValue(-84.3574);

    nam = new QNetworkAccessManager(this);
    factory = new geos::geom::GeometryFactory(new geos::geom::PrecisionModel(), 4326);

    connect(widget.pushButton, SIGNAL(clicked()), this, SLOT(setAddressFile()));
    connect(widget.pushButton_2, SIGNAL(clicked()), this, SLOT(convert()));
    connect(widget.pushButton_3, SIGNAL(clicked()), this, SLOT(setOutputFile()));
    connect(widget.pushButton_4, SIGNAL(clicked()), this, SLOT(setZipCodeFile()));
    connect(widget.pushButton_5, SIGNAL(clicked()), this, SLOT(setBuildingFile()));
    connect(nam, SIGNAL(finished(QNetworkReply*)), this, SLOT(readOSM(QNetworkReply*)));
}

QString MainForm::openFile() {
    return QFileDialog::getOpenFileName(this, tr("Open File"), "",
            tr("OSM File (*.osm);;XML File (*.xml)"));
}

void MainForm::setAddressFile() {
    QString fileName = openFile();
    if (!fileName.isEmpty()) {
        widget.lineEdit->setText(fileName);
    }
}

void MainForm::setZipCodeFile() {
    QString fileName = openFile();
    if (!fileName.isEmpty()) {
        widget.lineEdit_3->setText(fileName);
    }
}

void MainForm::setBuildingFile() {
    QString fileName = openFile();
    if (!fileName.isEmpty()) {
        widget.lineEdit_4->setText(fileName);
    }
}

void MainForm::setOutputFile() {
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"), "",
            tr("OsmChange File (*.osc)"));
    if (fileName.length() != 0) {
        if (fileName.endsWith(".osc")) {
            widget.lineEdit_2->setText(fileName);
        } else {
            widget.lineEdit_2->setText(fileName + ".osc");
        }

    }
}

void MainForm::convert() {
    widget.pushButton_2->setEnabled(false);
    qApp->processEvents();
    downloadOSM();
}

void MainForm::downloadOSM() {
    QString query = tr("[out:xml];"
            "(node[\"addr:housenumber\"](%1,%2,%3,%4);"
            "way[\"addr:housenumber\"](%1,%2,%3,%4);"
            "way[\"name\"](%1,%2,%3,%4);"
            "relation[\"addr:housenumber\"](%1,%2,%3,%4););"
            "out meta;>;out meta;")
            .arg(widget.doubleSpinBox_3->value())
            .arg(widget.doubleSpinBox_2->value())
            .arg(widget.doubleSpinBox->value())
            .arg(widget.doubleSpinBox_4->value());
    QUrl url("http://overpass-api.de/api/interpreter/");
    url.addQueryItem("data", query);
    nam->get(QNetworkRequest(url));
}

void MainForm::readOSM(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        QXmlStreamReader reader(reply->readAll());
        Street* street = NULL;
        Address* address = NULL;
        FeatureType current = None;
        while (!reader.atEnd()) {
            // Tags are assumed to be in alphabetical order
            switch (reader.readNext()) {
                case QXmlStreamReader::StartElement:
                    if (reader.name().toString() == "node") {
                        // Store all nodes
                        geos::geom::Coordinate coordinate;
                        uint nodeId = reader.attributes().value("id").toString().toUInt();
                        coordinate.y = reader.attributes().value("lat").toString().toDouble();
                        coordinate.x = reader.attributes().value("lon").toString().toDouble();
                        nodes.insert(nodeId, factory->createPoint(coordinate));
                    } else if (reader.name().toString() == "way") {
                        // A way is typically either be a road or a building. For
                        // now, assume it is a road, since we need the nodes and
                        // those come before the way tags
                        current = Way;
                        street = new Street();
                    } else if (reader.name().toString() == "tag") {
                        if (reader.attributes().value("k") == "addr:housenumber") {
                            delete address;
                            address = new Address();
                            address->houseNumber = reader.attributes().value("v").toString();
                        } else if (reader.attributes().value("k") == "addr:street") {
                            // We don't care what street instance the address is
                            // linked to. If there is any address with the same number
                            // and street name, skip it.
                            address->street.name = reader.attributes().value("v").toString();
                            existingAddresses.append(*address);
                            address = NULL;
                        } else if (current == Way && reader.attributes().value("k") == "building") {
                            // No buildings...yet.
                            current = None;
                            delete street;
                        } else if (reader.attributes().value("k") == "highway"
                                && current == Way) {
                            // We now know this is a way.
                            current = WayConfirmed;
                        } else if (reader.attributes().value("k") == "name"
                                && current == WayConfirmed) {
                            street->name = reader.attributes().value("v").toString();
                        }
                    } else if (reader.name().toString() == "nd" && current == Way) {
                        street->nodeIndices.append(reader.attributes().value("ref").toString().toUInt());
                    }
                    break;
                case QXmlStreamReader::EndElement:
                    if (current == WayConfirmed && reader.name().toString() == "way") {
                        streets.insertMulti(street->name.toUpper(), street);
                        current = None;
                    }
                    break;
                default:
                    break;
            }
        }
        QList<Street*> streetValues = streets.values();
        for (int i = 0; i < streetValues.size(); i++) {
            Street* street = streetValues.at(i);
            geos::geom::CoordinateSequence* nodePoints = factory
                    ->getCoordinateSequenceFactory()->create(
                    (std::vector<geos::geom::Coordinate>*) NULL, 2);
            for (int j = 0; j < street->nodeIndices.size(); j++) {
                nodePoints->add(*(nodes.value(street->nodeIndices.at(j))->getCoordinate()));
            }
            street->path = QSharedPointer<geos::geom::LineString>(factory->createLineString(nodePoints));
        }
        if (widget.checkBox->isChecked()) {
            widget.textBrowser->append("Streets:");
            QList<Street*> streetValues = streets.values();
            for (int i = 0; i < streetValues.size(); i++) {
                Street* street = streetValues.at(i);
                widget.textBrowser->append(street->name);
            }
        }
        if (widget.checkBox_2->isChecked()) {
            widget.textBrowser->append("");
            widget.textBrowser->append("Existing Addresses:");
            for (int i = 0; i < existingAddresses.size(); i++) {
                Address address = existingAddresses.at(i);
                widget.textBrowser->append(address.houseNumber + " " + address.street.name);
            }
        }
        readZipCodeFile();
    } else {
        widget.textBrowser->insertPlainText(reply->errorString());
        qCritical() << reply->errorString();
        cleanup();
    }
}

void MainForm::readZipCodeFile() {
    if (widget.lineEdit_3->text().isEmpty()) {
        readBuildingFile();
        return;
    }

    // Bypass reading the zip-code file for now.
    readBuildingFile();
    return;

    QHash<int, geos::geom::Point*> zipCodeNodes;
    QHash<int, geos::geom::LineString*> zipCodeWays;

    QFile file(widget.lineEdit_3->text());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "Error: Couldn't open " << widget.lineEdit_3->text();
        readBuildingFile();
        return;
    }

    QXmlStreamReader reader(&file);
    int wayId;
    int zipCode;
    bool polygon = false;
    geos::geom::CoordinateSequence* baseSequence = NULL;
    QList<geos::geom::Coordinate> coordinates;
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
            case QXmlStreamReader::StartElement:
                if (reader.name().toString() == "node") {
                    geos::geom::Coordinate coordinate;
                    coordinate.y = reader.attributes().value("lat").toString().toDouble();
                    coordinate.x = reader.attributes().value("lon").toString().toDouble();
                    int nodeId = reader.attributes().value("id").toString().toInt();
                    zipCodeNodes.insert(nodeId, factory->createPoint(coordinate));
                } else if (reader.name().toString() == "way") {
                    wayId = reader.attributes().value("id").toString().toInt();
                } else if (reader.name().toString() == "member") {
                    if (reader.attributes().value("role").toString() == "outer") {
                        geos::geom::CoordinateSequence* sequence = zipCodeWays
                                .value(reader.attributes().value("ref").toString().toInt())
                                ->getCoordinates();
                        if (baseSequence == NULL) {
                            baseSequence = sequence;
                        } else {
                            baseSequence->add(sequence, true, true);
                        }
                    }

                } else if (reader.name().toString() == "nd") {
                    coordinates.append(*zipCodeNodes.value(reader.attributes()
                            .value("k").toString().toInt())->getCoordinate());
                } else if (reader.name().toString() == "tag") {
                    if (reader.attributes().value("k") == "addr:postcode") {
                        polygon = true;
                        zipCode = reader.attributes().value("v").toString().toInt();
                    }
                }
                break;
            case QXmlStreamReader::EndElement:
                if (reader.name().toString() == "way") {

                    geos::geom::CoordinateSequence* sequence = factory
                                ->getCoordinateSequenceFactory()->create(
                                (std::vector<geos::geom::Coordinate>*) NULL, 2);
                    for (int i = 0; i < coordinates.size(); i++) {
                        sequence->add(coordinates.at(i));
                    }
                    if (polygon) {
                        geos::geom::LinearRing* ring = factory->createLinearRing(sequence);
                        std::vector<geos::geom::Geometry*> empty = QVector<geos::geom::Geometry*>().toStdVector();
                        zipCodes.insert(zipCode, factory->createPolygon(ring,
                                &empty));
                        zipCode = 0;
                        polygon = false;
                    }
                    zipCodeWays.insert(wayId, factory->createLineString(sequence));
                    coordinates.clear();
                }
                break;
            default:
                break;
        }
    }

    QList<geos::geom::Point*> zipCodeNodesPoints = zipCodeNodes.values();
    for (int i = 0; i < zipCodeNodesPoints.size(); i++) {
        delete zipCodeNodesPoints.at(i);
    }
    zipCodeNodes.clear();

    QList<geos::geom::LineString*> zipCodeWaysPoints = zipCodeWays.values();
    for (int i = 0; i < zipCodeWaysPoints.size(); i++) {
        delete zipCodeWaysPoints.at(i);
    }
    zipCodeWays.clear();

    readBuildingFile();
}

void MainForm::readBuildingFile() {
    QFile file(widget.lineEdit_4->text());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "Error: Couldn't open " << widget.lineEdit_4->text();
        cleanup();
        return;
    }

    double minLon = widget.doubleSpinBox_2->value();
    double minLat = widget.doubleSpinBox->value();
    double maxLon = widget.doubleSpinBox_4->value();
    double maxLat = widget.doubleSpinBox_3->value();

    QXmlStreamReader reader(&file);
    Building* building = NULL;
    geos::geom::CoordinateSequence* sequence;
    bool skip1 = true;
    bool skip2 = true;
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
            case QXmlStreamReader::StartElement:
                if (reader.name().toString() == "node") {
                    geos::geom::Coordinate coordinate;
                    coordinate.y = reader.attributes().value("lat").toString().toDouble();
                    coordinate.x = reader.attributes().value("lon").toString().toDouble();
                    int nodes = reader.attributes().value("id").toString().toInt();
                    buildingNodes.insert(nodes, factory->createPoint(coordinate));
                } else if (reader.name().toString() == "way") {
                    building = new Building();
                    sequence = factory->getCoordinateSequenceFactory()->create
                            ((std::vector<geos::geom::Coordinate>*) NULL, 2);
                } else if (reader.name().toString() == "nd") {
                    if (building != NULL) {
                        const geos::geom::Coordinate* coordinate = buildingNodes
                            .value(reader.attributes().value("ref")
                            .toString().toInt())->getCoordinate();
                        if (coordinate->x <= maxLon && coordinate->x >= minLon
                                && coordinate->y >= maxLat && coordinate->y <= minLat) {
                            skip2 = false;
                        }
                        sequence->add(*coordinate);
                    }
                } else if (reader.name().toString() == "tag" && building != NULL) {
                    if (reader.attributes().value("k") == "FeatureID") {
                        building->setFeatureID(reader.attributes().value("v").toString());
                        skip1 = false;
                    } else if (reader.attributes().value("k") == "YearBuilt") {
                        building->setYear(reader.attributes().value("v").toString().toInt());
                    }
                }
                break;
            case QXmlStreamReader::EndElement:
                if (reader.name().toString() == "way") {
                    if (building != NULL && !skip1 && !skip2) {
                        geos::geom::LinearRing* linearRing = factory->
                                createLinearRing(sequence);
                        building->setBuilding(QSharedPointer<geos::geom::Polygon>
                            (factory->createPolygon(linearRing, NULL)));
                        buildings.append(*building);
                    } else {
                        delete building;
                        delete sequence;
                    }
                    building = NULL;
                    sequence = NULL;
                    skip1 = true;
                    skip2 = true;
                }
                break;
            default:
                break;
        }
    }

    validateBuildings();
}

void MainForm::validateBuildings() {
    for (int i = 0; i < buildings.size(); i++) {
        Building building1 = buildings.at(i);
        geos::geom::prep::PreparedPolygon polygon(building1.getBuilding().data());
        for (int j = i + 1; j < buildings.size(); j++) {
            Building building2 = buildings.at(j);

            if (polygon.intersects(building2.getBuilding().data())) {
                if (building1.getYear() >= building2.getYear()) {
                    buildings.removeAt(j);
                    j--;
                } else {
                    buildings.removeAt(i);
                    i--;
                    break;
                }
            }
        }
    }

    simplifyBuildings();
}

void MainForm::simplifyBuildings() {
    for (int i = 0; i < buildings.size(); i++) {
        Building& building = buildings[i];
        geos::geom::Polygon* polygon = building.getBuilding().data();

        if (polygon->getArea() * DEGREES_TO_METERS * DEGREES_TO_METERS < 5) {
            buildings.removeAt(i);
            i--;
            continue;
        }

        geos::geom::CoordinateSequence* coordinates = polygon->getExteriorRing()
                ->getCoordinates();

        for (int j = 0; j < coordinates->size() - 1; j++) {
            geos::geom::Coordinate current = coordinates->getAt(j);
            geos::geom::Coordinate after = coordinates->getAt(j + 1);

            if (current.distance(after) * DEGREES_TO_METERS < 0.05) {
                if (j = coordinates->size() - 2) {
                    coordinates->deleteAt(j);
                } else {
                    coordinates->deleteAt(j + 1);
                }
            }
        }

        for (int j = 1; j < coordinates->size() - 1; j++) {
            geos::geom::Coordinate before = coordinates->getAt(j - 1);
            geos::geom::Coordinate current = coordinates->getAt(j);
            geos::geom::Coordinate after = coordinates->getAt(j + 1);

            double headingDiff = geos::algorithm::Angle::toDegrees(
                geos::algorithm::Angle::angleBetween(before,
                    current, after));

            if (qAbs(headingDiff - 180) < 5) {
                coordinates->deleteAt(j);
                j--;
            }
        }

        geos::geom::LinearRing* newLinearRing = factory->createLinearRing(coordinates);
        geos::geom::Polygon* newPolygon = factory->createPolygon(newLinearRing, NULL);
        building.setBuilding(QSharedPointer<geos::geom::Polygon>(newPolygon));
    }

    readAddressFile();
}

void MainForm::readAddressFile() {
    QFile file(widget.lineEdit->text());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "Error: Couldn't open " << widget.lineEdit->text();
        cleanup();
        return;
    }

    QXmlStreamReader reader(&file);
    Address* address;
    bool skip = false;
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
            case QXmlStreamReader::StartElement:
                if (reader.name().toString() == "node") {
                    address = new Address();
                    geos::geom::Coordinate coordinate;
                    coordinate.y = reader.attributes().value("lat").toString().toDouble();
                    coordinate.x = reader.attributes().value("lon").toString().toDouble();
                    address->coordinate = QSharedPointer<geos::geom::Point>(factory->createPoint(coordinate));
                    // Check to see if the address is inside the BBox
                    skip = !(coordinate.y <= widget.doubleSpinBox->value() &&
                            coordinate.y >= widget.doubleSpinBox_3->value() &&
                            coordinate.x >= widget.doubleSpinBox_2->value() &&
                            coordinate.x <= widget.doubleSpinBox_4->value());
                } else if (reader.name().toString() == "tag" && !skip) {
                    if (reader.attributes().value("k") == "addr:housenumber") {
                        address->houseNumber = reader.attributes().value("v").toString();
                    } else if (reader.attributes().value("k") == "addr:street") {
                        QString streetName = reader.attributes().value("v").toString();
                        QList<Street*> matches = streets.values(streetName.toUpper());
                        if (!matches.isEmpty()) {
                            Street* closestStreet = matches.at(0);
                            double minDistance = address->coordinate.data()->distance(closestStreet->path.data());
                            for (int i = 1; i < matches.size(); i++) {
                                double distance = address->coordinate.data()->distance(matches.at(i)->path.data());
                                if (distance < minDistance) {
                                    closestStreet = matches.at(i);
                                    minDistance = distance;
                                }
                            }
                            address->street = *closestStreet;
                        }
                    } else if (reader.attributes().value("k") == "addr:city") {
                        address->city = toTitleCase(reader.attributes().value("v").toString());
                    } else if (reader.attributes().value("k") == "addr:postcode") {
                        address->zipCode = reader.attributes().value("v").toString().toInt();
                    } else if (reader.attributes().value("k") == "import:FEAT_TYPE") {
                        if (reader.attributes().value("v") == "driv") {
                            address->addressType = Address::Primary;
                        } else if (reader.attributes().value("v") == "stru") {
                            address->addressType = Address::Structural;
                        }
                    }
                }
                break;
            case QXmlStreamReader::EndElement:
                if (reader.name().toString() == "node") {
                    if (!skip) {
                        if (!address->houseNumber.isEmpty()
                                && !address->street.name.isEmpty()
                                && !existingAddresses.contains(*address)
                                && address->addressType != Address::Other) {
                            int i = newAddresses.indexOf(*address);
                            if (i != -1) {
                                Address existingAddress = newAddresses.at(i);
                                if (existingAddress.addressType == Address::Structural
                                        && address->addressType == Address::Structural) {
                                    existingAddress.allowStructural = false;
                                } else if (existingAddress.addressType == Address::Primary
                                        && address->addressType == Address::Structural
                                        && existingAddress.allowStructural) {
                                    existingAddress.coordinate = address->coordinate;
                                    existingAddress.addressType = Address::Structural;
                                } else if (existingAddress.addressType == Address::Structural
                                        && address->addressType == Address::Primary
                                        && !existingAddress.allowStructural) {
                                    existingAddress.coordinate = address->coordinate;
                                    existingAddress.addressType = Address::Primary;
                                }
                                delete address;
                            } else {
                                newAddresses.append(*address);
                            }
                        } else {
                            delete address;
                        }
                    } else {
                        delete address;
                    }
                }
                break;
            default:
                break;
        }
    }
    if (widget.checkBox_3->isChecked()) {
        widget.textBrowser->append("");
        widget.textBrowser->append("New Addresses (before validation):");
        for (int i = 0; i < newAddresses.size(); i++) {
            Address address = newAddresses.at(i);
            widget.textBrowser->append(address.houseNumber + " " + address.street.name);
        }
    }
    validateAddresses();
}

void MainForm::validateAddresses() {
    if (widget.checkBox_4->isChecked() || widget.checkBox_5->isChecked() || widget.checkBox_6->isChecked()) {
        widget.textBrowser->append("");
        widget.textBrowser->append("Address Validations");
    }
    for (int i = 0; i < newAddresses.size(); i++) {
        Address address = newAddresses.at(i);

        double distance = address.coordinate.data()->distance(address.street.path.data()) * DEGREES_TO_METERS;

        if (distance > 100) {
            if (widget.checkBox_5->isChecked()) {
                widget.textBrowser->append(tr("Too far from the street: %1 %2").arg(address.houseNumber)
                        .arg(address.street.name));
            }
            newAddresses.removeOne(address);
            excludedAddresses.append(address);
            i--;
        } else if (distance < 5) {
            if (widget.checkBox_6->isChecked()) {
                widget.textBrowser->append(tr("Too close to the street: %1 %2").arg(address.houseNumber)
                        .arg(address.street.name));
            }
            newAddresses.removeOne(address);
            excludedAddresses.append(address);
            i--;
        }
    }
    validateBetweenAddresses();

    if (widget.checkBox_7->isChecked()) {
        widget.textBrowser->append("");
        widget.textBrowser->append("New Addresses (after validation):");
        for (int i = 0; i < newAddresses.size(); i++) {
            Address address = newAddresses.at(i);
            widget.textBrowser->append(address.houseNumber + " " + address.street.name);
        }
    }

    mergeAddressBuilding();
}

void MainForm::validateBetweenAddresses() {
    int sections = 2;
    double lonSection = (widget.doubleSpinBox_4->value() - widget.doubleSpinBox_2->value()) / sections;
    double latSection = (widget.doubleSpinBox->value() - widget.doubleSpinBox_3->value()) / sections;

    for (int region = 0; region < sections * sections; region++) {
        double minLon = widget.doubleSpinBox_2->value() + (region % sections) * lonSection;
        double minLat = widget.doubleSpinBox->value() - (region / sections) * latSection;
        double maxLon = minLon + lonSection;
        double maxLat = minLat - latSection;

        for (int i = 0; i < newAddresses.size(); i++) {
            Address address1 = newAddresses.at(i);
            const geos::geom::Coordinate* coordinate1 = address1.coordinate.data()->getCoordinate();
            if (!(coordinate1->x <= maxLon && coordinate1->x >= minLon
                    && coordinate1->y >= maxLat && coordinate1->y <= minLat)) {
                continue;
            }

            for (int j = i + 1; j < newAddresses.size(); j++) {
                Address address2 = newAddresses.at(j);
                const geos::geom::Coordinate* coordinate2 = address2.coordinate.data()->getCoordinate();
                if (!(coordinate2->x <= maxLon && coordinate2->x >= minLon
                        && coordinate2->y >= maxLat && coordinate2->y <= minLat)) {
                    continue;
                }

                double distance = address1.coordinate.data()->distance(address2.coordinate.data()) * 111000;

                if (distance < 4) {
                    if (widget.checkBox_4->isChecked()) {
                        widget.textBrowser->append(tr("Too close to another address: %1 %2")
                                .arg(address2.houseNumber).arg(address2.street.name));
                    }
                    newAddresses.removeOne(address2);
                    excludedAddresses.append(address2);
                    j--;
                }
            }
        }
    }
}

void MainForm::mergeAddressBuilding() {
    for (int i = 0; i < buildings.size(); i++) {
        Building building = buildings.at(i);
        bool addressSet = false;

        geos::geom::prep::PreparedPolygon polygon(building.getBuilding().data());

        for (int j = 0; j < newAddresses.size(); j++) {
            Address address = newAddresses.at(j);

            if (polygon.contains(address.coordinate.data())) {
                if (!addressSet) {
                    addressBuildings.insertMulti(address, building);
                    addressSet = true;
                } else {
                    addressBuildings.remove(addressBuildings.key(building));
                    break;
                }
            }
        }
    }

    QList<Address> mergedAddresses = addressBuildings.keys();
    for (int i = 0; i < mergedAddresses.size(); i++) {
        newAddresses.removeOne(mergedAddresses.at(i));
    }

    QList<Building> mergedBuildings = addressBuildings.values();
    for (int i = 0; i < mergedBuildings.size(); i++) {
        buildings.removeOne(mergedBuildings.at(i));
    }

    outputChangeFile();
}

void MainForm::outputChangeFile() {
    if (widget.lineEdit_2->text().isEmpty()) {
        cleanup();
        return;
    }
    QString fullFileName = widget.lineEdit_2->text();
    QFile file(fullFileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        writeXMLFile(file, &newAddresses, &buildings, &addressBuildings);
    }

    for (int i = 0; i <= excludedAddresses.size() / 5000; i++) {
        QString fullFileName = widget.lineEdit_2->text();
        if (i == 0) {
            if (widget.lineEdit_2->text().lastIndexOf(".") == -1) {
                fullFileName = tr("%1_errors").arg(widget.lineEdit_2->text());
            }
            else {
                QString baseName = widget.lineEdit_2->text().left(widget.lineEdit_2
                        ->text().lastIndexOf("."));
                QString extension = widget.lineEdit_2->text().right(widget.lineEdit_2->text().length()
                        - widget.lineEdit_2->text().lastIndexOf(".") - 1);
                fullFileName = tr("%1_errors.%2").arg(baseName).arg(extension);
            }
        } else {
            if (widget.lineEdit_2->text().lastIndexOf(".") == -1) {
                fullFileName = tr("%1_errors_%2").arg(widget.lineEdit_2->text()).arg(i);
            }
            else {
                QString baseName = widget.lineEdit_2->text().left(widget.lineEdit_2
                        ->text().lastIndexOf("."));
                QString extension = widget.lineEdit_2->text().right(widget.lineEdit_2->text().length()
                        - widget.lineEdit_2->text().lastIndexOf(".") - 1);
                fullFileName = tr("%1_errors_%2.%3").arg(baseName).arg(i).arg(extension);
            }
        }
        QFile file(fullFileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            continue;
        }

        writeXMLFile(file, &excludedAddresses, NULL, NULL);
    }

    // Output the log to a file
    QString baseName = widget.lineEdit_2->text().left(widget.lineEdit_2->text()
            .lastIndexOf("."));
    QFile logFile(baseName + ".log");
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream writer(&logFile);
        writer << widget.textBrowser->document()->toPlainText();
        writer.flush();
    }

    cleanup();
}

void MainForm::writeXMLFile(QFile& file, QList<Address>* addresses,
        QList<Building>* buildings, QHash<Address, Building>* addressBuildings) {
    QXmlStreamWriter writer(&file);
    writer.setAutoFormatting(true);
    outputStartOfFile(writer);
    uint id = 1;

    if (addresses != NULL) {
        for (int i = 0; i < addresses->size(); i++) {
            Address address = addresses->at(i);

            writer.writeStartElement("node");
            writer.writeAttribute("id", tr("-%1").arg(id));
            id++;
            writer.writeAttribute("lat", QString::number(address.coordinate.data()->getY(), 'g', 12));
            writer.writeAttribute("lon", QString::number(address.coordinate.data()->getX(), 'g', 12));

            writer.writeStartElement("tag");
            writer.writeAttribute("k", "addr:housenumber");
            writer.writeAttribute("v", address.houseNumber);
            writer.writeEndElement();

            writer.writeStartElement("tag");
            writer.writeAttribute("k", "addr:street");
            writer.writeAttribute("v", address.street.name);
            writer.writeEndElement();

            if (!address.city.isEmpty()) {
                writer.writeStartElement("tag");
                writer.writeAttribute("k", "addr:city");
                writer.writeAttribute("v", address.city);
                writer.writeEndElement();
            }

            if (address.zipCode != 0) {
                writer.writeStartElement("tag");
                writer.writeAttribute("k", "addr:postcode");
                writer.writeAttribute("v", QString::number(address.zipCode));
                writer.writeEndElement();
            }

            writer.writeEndElement();
        }
    }

    if (buildings != NULL) {
        for (int i = 0; i < buildings->size(); i++) {
            Building building = buildings->at(i);

            const geos::geom::CoordinateSequence* coordinates = building.getBuilding()
                    .data()->getExteriorRing()->getCoordinatesRO();
            for (int j = 0; j < coordinates->size() - 1; j++) {
                geos::geom::Coordinate coordinate = coordinates->getAt(j);
                writer.writeStartElement("node");
                writer.writeAttribute("id", tr("-%1").arg(id));
                id++;
                writer.writeAttribute("lat", QString::number(coordinate.y, 'g', 12));
                writer.writeAttribute("lon", QString::number(coordinate.x, 'g', 12));
                writer.writeEndElement();
            }

            writer.writeStartElement("way");
            writer.writeAttribute("id", tr("-%1").arg(id));
            id++;

            for (int j = coordinates->size() - 1; j >= 1; j--) {
                writer.writeStartElement("nd");
                writer.writeAttribute("ref", tr("-%1").arg(id - (j + 1)));
                writer.writeEndElement();
            }
            writer.writeStartElement("nd");
            writer.writeAttribute("ref", tr("-%1").arg(id - coordinates->size()));
            writer.writeEndElement();

            writer.writeStartElement("tag");
            writer.writeAttribute("k", "building");
            writer.writeAttribute("v", "yes");
            writer.writeEndElement();

            writer.writeEndElement();
        }
    }

    if (addressBuildings != NULL) {
        QList<Address> mergedAddresses = addressBuildings->keys();
        QList<Building> mergedBuildings = addressBuildings->values();
        for (int i = 0; i < mergedBuildings.size(); i++) {
            Address address = mergedAddresses.at(i);
            Building building = mergedBuildings.at(i);

            const geos::geom::CoordinateSequence* coordinates = building.getBuilding()
                    .data()->getExteriorRing()->getCoordinatesRO();
            for (int j = 0; j < coordinates->size() - 1; j++) {
                geos::geom::Coordinate coordinate = coordinates->getAt(j);
                writer.writeStartElement("node");
                writer.writeAttribute("id", tr("-%1").arg(id));
                id++;
                writer.writeAttribute("lat", QString::number(coordinate.y, 'g', 12));
                writer.writeAttribute("lon", QString::number(coordinate.x, 'g', 12));
                writer.writeEndElement();
            }

            writer.writeStartElement("way");
            writer.writeAttribute("id", tr("-%1").arg(id));
            id++;

            for (int j = coordinates->size() - 1; j >= 1; j--) {
                writer.writeStartElement("nd");
                writer.writeAttribute("ref", tr("-%1").arg(id - (j + 1)));
                writer.writeEndElement();
            }
            writer.writeStartElement("nd");
            writer.writeAttribute("ref", tr("-%1").arg(id - coordinates->size()));
            writer.writeEndElement();

            writer.writeStartElement("tag");
            writer.writeAttribute("k", "building");
            writer.writeAttribute("v", "yes");
            writer.writeEndElement();

            writer.writeStartElement("tag");
            writer.writeAttribute("k", "addr:housenumber");
            writer.writeAttribute("v", address.houseNumber);
            writer.writeEndElement();

            writer.writeStartElement("tag");
            writer.writeAttribute("k", "addr:street");
            writer.writeAttribute("v", address.street.name);
            writer.writeEndElement();

            if (!address.city.isEmpty()) {
                writer.writeStartElement("tag");
                writer.writeAttribute("k", "addr:city");
                writer.writeAttribute("v", address.city);
                writer.writeEndElement();
            }

            if (address.zipCode != 0) {
                writer.writeStartElement("tag");
                writer.writeAttribute("k", "addr:postcode");
                writer.writeAttribute("v", QString::number(address.zipCode));
                writer.writeEndElement();
            }

            writer.writeEndElement();
        }
    }

    outputEndOfFile(writer);
}

void MainForm::outputStartOfFile(QXmlStreamWriter& writer) {
    writer.writeStartDocument();
    writer.writeStartElement("osmChange");
    writer.writeAttribute("version", "0.6");
    writer.writeStartElement("create");
}

void MainForm::outputEndOfFile(QXmlStreamWriter& writer) {
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndDocument();
}

void MainForm::cleanup() {
    QList<geos::geom::Point*> pointValues = nodes.values();
    for (int i = 0; i < pointValues.size(); i++) {
        geos::geom::Point* point = pointValues.at(i);
        delete point;
    }
    nodes.clear();
    QList<geos::geom::Point*> buildingPointValues = buildingNodes.values();
    for (int i = 0; i < buildingPointValues.size(); i++) {
        geos::geom::Point* point = buildingPointValues.at(i);
        delete point;
    }
    buildingNodes.clear();
    QList<Street*> streetValues = streets.values();
    for (int i = 0; i < streetValues.size(); i++) {
        Street* street = streetValues.at(i);
        delete street;
    }
    streets.clear();
    existingAddresses.clear();
    newAddresses.clear();
    excludedAddresses.clear();
    addressBuildings.clear();

    widget.pushButton_2->setEnabled(true);
}

QString MainForm::expandQuadrant(QString street) {
    return street.replace("NE", "Northeast").replace("NW", "Northwest")
            .replace("SE", "Southeast").replace("SW", "Southwest");
}

QString MainForm::toTitleCase(QString str) {
    QRegExp re("\\W\\w");
    int pos = -1;
    str = str.toLower();
    QChar *base = str.data();
    QChar *ch;
    do {
        pos++;
        ch = base + pos;
        pos = str.indexOf(re, pos);
        *ch = ch->toUpper();
    } while (pos >= 0);
    return str;
}

MainForm::~MainForm() {
    delete nam;
    delete factory;
}
