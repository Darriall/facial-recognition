/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright 2012 The MITRE Corporation                                      *
 *                                                                           *
 * Licensed under the Apache License, Version 2.0 (the "License");           *
 * you may not use this file except in compliance with the License.          *
 * You may obtain a copy of the License at                                   *
 *                                                                           *
 *     http://www.apache.org/licenses/LICENSE-2.0                            *
 *                                                                           *
 * Unless required by applicable law or agreed to in writing, software       *
 * distributed under the License is distributed on an "AS IS" BASIS,         *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
 * See the License for the specific language governing permissions and       *
 * limitations under the License.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <QDate>
#ifndef BR_EMBEDDED
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtXml>
#endif // BR_EMBEDDED
#include <opencv2/highgui/highgui.hpp>
#include <openbr_plugin.h>

#include "core/bee.h"
#include "core/opencvutils.h"
#include "core/qtutils.h"

using namespace br;
using namespace cv;

/*!
 * \ingroup formats
 * \brief Reads a comma separated value file.
 * \author Josh Klontz \cite jklontz
 */
class csvFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        QFile f(file.name);
        f.open(QFile::ReadOnly);
        QStringList lines(QString(f.readAll()).split('\n'));
        f.close();

        QList< QList<float> > valsList;
        foreach (const QString &line, lines) {
            QList<float> vals;
            foreach (const QString &word, line.split(QRegExp(" *, *"))) {
                bool ok;
                vals.append(word.toFloat(&ok)); assert(ok);
            }
            valsList.append(vals);
        }

        assert((valsList.size() > 0) && (valsList[0].size() > 0));
        Mat m(valsList.size(), valsList[0].size(), CV_32FC1);
        for (int i=0; i<valsList.size(); i++) {
            assert(valsList[i].size() == valsList[0].size());
            for (int j=0; j<valsList[i].size(); j++) {
                m.at<float>(i,j) = valsList[i][j];
            }
        }

        return Template(m);
    }

    void write(const Template &t) const
    {
        const Mat &m = t.m();
        if (t.size() != 1) qFatal("csvFormat::write only supports single matrix templates.");
        if (m.channels() != 1) qFatal("csvFormat::write only supports single channel matrices.");

        QStringList lines; lines.reserve(m.rows);
        for (int r=0; r<m.rows; r++) {
            QStringList elements; elements.reserve(m.cols);
            for (int c=0; c<m.cols; c++)
                elements.append(OpenCVUtils::elemToString(m, r, c));
            lines.append(elements.join(","));
        }

        QtUtils::writeFile(file, lines);
    }
};

BR_REGISTER(Format, csvFormat)

/*!
 * \ingroup formats
 * \brief Reads image files.
 * \author Josh Klontz \cite jklontz
 */
class DefaultFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        Template t;

        if (file.name.startsWith("http://") || file.name.startsWith("www.")) {
#ifndef BR_EMBEDDED
            QNetworkAccessManager networkAccessManager;
            QNetworkRequest request(file.name);
            request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
            QNetworkReply *reply = networkAccessManager.get(request);

            while (!reply->isFinished()) QCoreApplication::processEvents();
            if (reply->error()) qWarning("Url::read %s (%s).\n", qPrintable(reply->errorString()), qPrintable(QString::number(reply->error())));

            QByteArray data = reply->readAll();
            delete reply;

            Mat m = imdecode(Mat(1, data.size(), CV_8UC1, data.data()), 1);
            if (m.data) t.append(m);
#endif // BR_EMBEDDED
        } else {
            QString prefix = "";
            if (!QFileInfo(file.name).exists()) prefix = file.getString("path") + "/";
            Mat m = imread((prefix+file.name).toStdString());
            if (m.data) t.append(m);
        }

        return t;
    }

    void write(const Template &t) const
    {
        imwrite(file.name.toStdString(), t);
    }
};

BR_REGISTER(Format, DefaultFormat)

/*!
 * \ingroup formats
 * \brief Reads a NIST BEE similarity matrix.
 * \author Josh Klontz \cite jklontz
 */
class mtxFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        return BEE::readSimmat(file);
    }

    void write(const Template &t) const
    {
        BEE::writeSimmat(t, file);
    }
};

BR_REGISTER(Format, mtxFormat)

/*!
 * \ingroup formats
 * \brief Reads a NIST BEE mask matrix.
 * \author Josh Klontz \cite jklontz
 */
class maskFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        return BEE::readMask(file);
    }

    void write(const Template &t) const
    {
        BEE::writeMask(t, file);
    }
};

BR_REGISTER(Format, maskFormat)

/*!
 * \ingroup formats
 * \brief Retrieves an image from a webcam.
 * \author Josh Klontz \cite jklontz
 */
class webcamFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        static QScopedPointer<VideoCapture> videoCapture;

        if (videoCapture.isNull())
            videoCapture.reset(new VideoCapture(0));

        Mat m;
        videoCapture->read(m);
        return Template(m);
    }

    void write(const Template &t) const
    {
        (void) t;
        qFatal("webcamFormat::write not supported.");
    }
};

BR_REGISTER(Format, webcamFormat)

#ifndef BR_EMBEDDED
/*!
 * \ingroup formats
 * \brief Decodes images from Base64 xml
 * \author Scott Klum \cite sklum
 * \author Josh Klontz \cite jklontz
 */
class xmlFormat : public Format
{
    Q_OBJECT

    Template read() const
    {
        QDomDocument doc(file);
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) qFatal("xmlFormat::read unable to open %s for reading.", qPrintable(file.flat()));
        if (!doc.setContent(&f))          qFatal("xmlFormat::read unable to parse %s.", qPrintable(file.flat()));
        f.close();

        Template t;
        QDomElement docElem = doc.documentElement();
        QDomNode subject = docElem.firstChild();
        while (!subject.isNull()) {
            QDomNode fileNode = subject.firstChild();

            while (!fileNode.isNull()) {
                QDomElement e = fileNode.toElement();

                if (e.tagName() == "FORMAL_IMG") {
                    QByteArray byteArray = QByteArray::fromBase64(qPrintable(e.text()));
                    Mat m = imdecode(Mat(1, byteArray.size(), CV_8UC1, byteArray.data()), CV_LOAD_IMAGE_ANYDEPTH);
                    if (!m.data) qWarning("xmlFormat::read failed to decode image data.");
                    t.append(m);
                } else if ((e.tagName() == "RELEASE_IMG") ||
                           (e.tagName() == "PREBOOK_IMG") ||
                           (e.tagName() == "LPROFILE") ||
                           (e.tagName() == "RPROFILE")) {
                    // Ignore these other image fields for now
                } else {
                    t.file.insert(e.tagName(), e.text());
                }

                fileNode = fileNode.nextSibling();
            }
            subject = subject.nextSibling();
        }

        // Calculate age
        if (t.file.contains("DOB")) {
            const QDate dob = QDate::fromString(t.file.getString("DOB").left(10), "yyyy-MM-dd");
            const QDate current = QDate::currentDate();
            int age = current.year() - dob.year();
            if (current.month() < dob.month()) age--;
            t.file.insert("Age", age);
        }

        return t;
    }

    void write(const Template &t) const
    {
        (void) t;
        qFatal("xmlFormat::write not supported.");
    }
};

BR_REGISTER(Format, xmlFormat)
#endif // BR_EMBEDDED

#include "format.moc"
