#include "ui/MenuStyle.h"
#include "ui/HomeMenuLayout.h"
#include <QByteArray>
#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace headunit::menu {
namespace {
// The design draws each tile's stripes, symbol and frame on an artboard of its own, stretched onto the tile.
const QRectF kArtboard(56.816, 70.204, 228.3, 392.707);
const QPointF kTitleOffset(7.249, 62.337);   // start of the tile title's baseline, from the tile's top left corner

// Outlines copied from the design (artboard coordinates). The two stripes are the same on every tile.
const char* const kStripes =
    "M172.688,70.204L285.116,70.204L285.116,203.986L172.688,70.204ZM230.079,462.911L56.816,462.911L56.816,256.741"
    "L230.079,462.911Z";
const char* const kAndroidAutoIcon =
    "M170.966,192.925C167.542,192.925 164.631,194.888 163.033,197.834L105.216,305.828"
    "C104.414,307.32 103.992,309.014 103.992,310.737C103.992,316.123 108.115,320.555 113.124,320.555"
    "C113.133,320.555 113.141,320.555 113.149,320.555L124.564,320.555L168.512,237.227L170.966,232.502L217.368,320.555"
    "L228.783,320.555C228.791,320.555 228.8,320.555 228.808,320.555C233.818,320.555 237.94,316.123 237.94,310.737"
    "C237.94,309.014 237.518,307.32 236.717,305.828L178.9,197.834C177.266,194.792 174.234,192.915 170.966,192.925Z"
    "M170.966,238.761L119.199,336.938L122.281,340.19L170.966,320.555L219.651,340.19L222.733,336.938L170.966,238.761Z";
const char* const kMultimediaIcon =
    "M210.85,286.448C213.905,285.873 216.792,285.969 219.322,286.604L219.322,222.669L156.397,242.095L156.397,316.336"
    "C156.43,316.744 156.453,317.163 156.453,317.571L156.453,317.583C156.453,327.577 146.699,337.5 134.66,339.741"
    "C122.633,341.97 112.868,335.679 112.868,325.672C112.868,315.677 122.633,305.754 134.66,303.525"
    "C139.186,302.687 143.4,303.058 146.889,304.376L146.889,212.986L147.412,212.986L228.842,192.925L228.842,295.892"
    "C228.987,296.647 229.065,297.414 229.065,298.181L229.065,298.193C229.065,306.545 220.905,314.838 210.861,316.696"
    "C200.807,318.565 192.658,313.304 192.658,304.952C192.647,296.599 200.796,288.306 210.85,286.448Z";
const char* const kRadioIcon =
    "M218.67,261.374C218.67,257.526 215.771,254.422 212.204,254.422C208.625,254.422 205.738,257.538 205.738,261.374"
    "C205.738,275.073 202.683,286.387 196.496,293.938C190.888,300.77 182.416,304.713 170.966,304.713"
    "C159.517,304.713 151.044,300.782 145.436,293.95C139.238,286.399 136.194,275.085 136.194,261.374"
    "C136.194,257.526 133.296,254.422 129.728,254.422C126.15,254.422 123.262,257.538 123.262,261.374"
    "C123.262,278.453 127.376,292.931 135.737,303.131C141.891,310.634 150.186,315.716 160.632,317.693L160.632,329.103"
    "L144.779,329.103C141.947,329.103 139.628,331.596 139.628,334.641L139.628,340.19L202.327,340.19L202.327,334.641"
    "C202.327,331.596 200.008,329.103 197.176,329.103L181.29,329.103L181.29,317.693"
    "C191.736,315.704 200.019,310.634 206.184,303.119C214.545,292.931 218.67,278.453 218.67,261.374ZM170.955,192.925"
    "C184.155,192.925 194.946,204.539 194.946,218.718L194.946,219.389L182.048,219.389L182.048,235.821L194.957,235.821"
    "L194.957,244.187L182.048,244.187L182.048,260.619L194.957,260.619L194.957,266.527"
    "C194.957,280.718 184.155,292.32 170.966,292.32C157.767,292.32 146.975,280.706 146.975,266.527L146.975,260.619"
    "L159.885,260.619L159.885,244.187L146.964,244.187L146.964,235.821L159.874,235.821L159.874,219.389L146.964,219.389"
    "L146.964,218.718C146.964,204.539 157.755,192.925 170.955,192.925Z";
const char* const kTelephoneIcon =
    "M106.303,217.75C116.812,225.512 127.237,233.317 136.037,243.368C130.993,267.322 151.126,286.46 169.487,297.736"
    "C176.675,302.15 179.852,305.244 187.854,303.757L219.367,336.95C160.063,358.362 93.509,268.53 106.303,217.75Z"
    "M195.978,295.222L202.89,287.674C204.884,285.494 208.174,285.467 210.202,287.611L235.688,314.587"
    "C237.715,316.731 237.741,320.269 235.747,322.448L228.832,329.997C226.838,332.176 223.547,332.204 221.519,330.06"
    "L196.033,303.083C194.006,300.94 193.981,297.402 195.978,295.222ZM112.514,201.874L117.531,195.326"
    "C119.681,192.519 123.579,192.114 126.189,194.425L152.281,217.533C154.891,219.848 155.265,224.035 153.117,226.846"
    "L148.098,233.39C145.947,236.201 142.05,236.602 139.438,234.29L113.352,211.183"
    "C110.741,208.871 110.362,204.681 112.514,201.874Z";
const char* const kNavigationIcon =
    "M222.872,237.328C220.698,239.892 218.168,242.181 215.325,244.039C214.979,244.315 214.5,244.351 214.11,244.075"
    "C209.919,241.211 206.385,237.759 203.62,234.008C199.797,228.855 197.389,223.138 196.564,217.637"
    "C195.728,212.053 196.508,206.684 199.094,202.309C200.12,200.595 201.424,199.014 203.007,197.659"
    "C206.652,194.543 210.81,192.889 214.968,192.925C218.959,192.961 222.905,194.555 226.316,197.899"
    "C227.52,199.073 228.524,200.404 229.348,201.866C232.124,206.791 232.726,213.059 231.5,219.423"
    "C230.296,225.715 227.308,232.102 222.872,237.328ZM136.446,303.254"
    "C134.272,305.819 131.742,308.108 128.899,309.965C128.554,310.241 128.074,310.277 127.684,310.001"
    "C123.493,307.125 119.959,303.685 117.194,299.934C113.382,294.793 110.974,289.076 110.149,283.564"
    "C109.313,277.979 110.093,272.61 112.68,268.247C113.694,266.522 114.998,264.952 116.592,263.598"
    "C120.238,260.482 124.396,258.828 128.542,258.864C132.533,258.9 136.479,260.494 139.89,263.837"
    "C141.094,265.012 142.098,266.342 142.923,267.804C145.698,272.73 146.3,278.997 145.074,285.361"
    "C143.87,291.641 140.883,298.029 136.446,303.254ZM128.855,316.413C133.882,316.413 138.129,320.032 139.467,324.97"
    "L206.318,324.97C209.618,324.97 212.627,323.52 214.812,321.171C216.997,318.822 218.346,315.598 218.346,312.039"
    "C218.346,308.491 216.997,305.255 214.812,302.906C212.627,300.557 209.629,299.107 206.318,299.107L182.931,299.107"
    "C177.68,299.107 172.909,296.806 169.453,293.091C165.998,289.376 163.857,284.247 163.857,278.602"
    "C163.857,272.957 165.998,267.828 169.453,264.113C172.909,260.398 177.68,258.097 182.931,258.097L204.077,258.097"
    "C205.526,253.351 209.684,249.923 214.578,249.923C220.687,249.923 225.636,255.244 225.636,261.812"
    "C225.636,268.379 220.687,273.7 214.578,273.7C209.729,273.7 205.616,270.345 204.122,265.683L182.931,265.683"
    "C179.631,265.683 176.621,267.133 174.436,269.482C172.251,271.831 170.903,275.055 170.903,278.614"
    "C170.903,282.161 172.251,285.397 174.436,287.746C176.599,290.071 179.575,291.521 182.853,291.545L206.329,291.545"
    "C211.579,291.545 216.351,293.846 219.806,297.561C223.262,301.277 225.402,306.406 225.402,312.05"
    "C225.402,317.695 223.262,322.824 219.806,326.54C216.351,330.255 211.579,332.556 206.329,332.556L139.188,332.556"
    "C137.594,337.026 133.57,340.19 128.855,340.19C122.746,340.19 117.796,334.869 117.796,328.301"
    "C117.796,321.734 122.746,316.413 128.855,316.413ZM127.751,268.787"
    "C132.678,268.787 136.669,273.077 136.669,278.374C136.669,283.671 132.678,287.962 127.751,287.962"
    "C122.824,287.962 118.833,283.671 118.833,278.374C118.833,273.077 122.824,268.787 127.751,268.787Z"
    "M214.177,202.849C219.104,202.849 223.095,207.139 223.095,212.436C223.095,217.733 219.104,222.024 214.177,222.024"
    "C209.25,222.024 205.259,217.733 205.259,212.436C205.259,207.139 209.25,202.849 214.177,202.849Z";
const char* const kVehicleIcon =
    "M170.966,193.5C208.789,193.5 239.456,226.218 239.456,266.557C239.456,306.909 208.789,339.615 170.966,339.615"
    "C133.143,339.615 102.476,306.909 102.476,266.557C102.476,226.206 133.143,193.5 170.966,193.5ZM234.997,266.557"
    "C234.997,228.83 206.337,198.246 170.966,198.246C135.606,198.246 106.935,228.83 106.935,266.557"
    "C106.935,304.284 135.595,334.869 170.966,334.869C206.326,334.869 234.997,304.284 234.997,266.557Z"
    "M142.039,231.107C138.951,234.583 137.345,236.309 133.433,240.935L121.594,228.962"
    "C122.72,227.392 124.615,224.947 125.919,223.401C130.233,218.284 131.805,216.786 133.867,216.558"
    "C134.971,216.438 136.064,216.714 136.933,217.469C138.717,219.003 138.561,221.244 138.315,221.939L138.26,222.095"
    "L138.427,222.023C140.077,221.292 142.083,221.616 143.276,223.09C145.472,225.798 144.235,228.651 142.039,231.107Z"
    "M126.599,228.183L128.896,230.52C128.896,230.52 133.522,225.367 134.782,223.845"
    "C135.339,223.174 135.841,222.359 135.662,221.508C135.484,220.657 134.681,220.07 133.901,220.297"
    "C133.422,220.429 133.031,220.789 132.664,221.136C131.515,222.251 128.985,225.067 126.599,228.183Z"
    "M131.237,232.869L133.645,235.302L139.608,228.483C140.065,227.955 140.623,227.272 140.768,226.541"
    "C140.89,225.93 140.667,225.319 140.166,224.959C139.653,224.6 139.096,224.648 138.572,224.923"
    "C138.17,225.127 137.791,225.523 137.212,226.17C135.74,227.788 131.237,232.869 131.237,232.869ZM130.345,266.557"
    "C130.345,242.612 148.515,223.234 170.966,223.234C193.395,223.234 211.587,242.636 211.587,266.557"
    "C211.587,290.502 193.417,309.881 170.966,309.881C148.526,309.881 130.345,290.478 130.345,266.557Z"
    "M170.966,266.557L209.358,266.557C209.358,243.979 192.135,225.606 170.966,225.606L170.966,266.557Z"
    "M170.966,266.557L132.574,266.557C132.574,289.136 149.797,307.508 170.966,307.508L170.966,266.557Z"
    "M204.163,235.566L208.444,226.709L208.834,226.146L208.31,226.565L200.016,231.155"
    "C199.08,230.232 197.609,228.89 196.494,227.979L205.3,213.203C206.549,214.233 207.53,215.084 208.555,216.007"
    "L202.748,225.367L202.235,226.002L202.881,225.523L210.629,221.388L213.371,224.312L209.492,232.569L209.046,233.265"
    "L209.648,232.713L218.432,226.517C219.257,227.56 220.361,229.022 221.052,229.993L207.184,239.389"
    "C206.437,238.346 205.089,236.632 204.163,235.566ZM172.694,216.834L169.238,216.834L165.448,207.774"
    "L165.203,206.971L165.27,207.81L164.869,219.914C163.531,220.058 162.16,220.249 160.811,220.489L161.424,202.56"
    "C163.319,202.333 165.203,202.177 167.087,202.093L170.821,211.98L170.955,212.711L171.089,211.98L174.823,202.093"
    "C176.707,202.177 178.591,202.333 180.486,202.56L181.11,220.501C179.762,220.261 178.379,220.058 177.053,219.926"
    "L176.651,207.822L176.718,206.983L176.484,207.786L172.694,216.834Z";
const char* const kSettingsIcon =
    "M215.723,210.543L225.553,221.113C228.14,223.894 228.14,228.445 225.553,231.226L217.637,239.737"
    "C219.821,244.12 221.487,248.846 222.548,253.821L232.802,253.821C236.462,253.821 239.456,257.04 239.456,260.973"
    "L239.456,275.919C239.456,279.854 236.462,283.073 232.802,283.073L221.611,283.073"
    "C220.239,287.942 218.276,292.536 215.81,296.757L223.069,304.559C225.657,307.343 225.657,311.893 223.069,314.674"
    "L213.239,325.243C210.652,328.023 206.418,328.023 203.832,325.243L195.915,316.731"
    "C191.838,319.08 187.442,320.873 182.813,322.012L182.813,333.036C182.813,336.97 179.82,340.189 176.16,340.189"
    "L162.259,340.189C158.6,340.189 155.605,336.97 155.605,333.036L155.605,321.005"
    "C151.076,319.529 146.803,317.418 142.878,314.767L135.62,322.571C133.031,325.353 128.798,325.353 126.21,322.571"
    "L116.381,312.003C113.793,309.222 113.793,304.671 116.381,301.89L124.298,293.378"
    "C122.113,288.995 120.445,284.27 119.387,279.295L109.129,279.295C105.47,279.295 102.476,276.077 102.476,272.143"
    "L102.476,257.197C102.476,253.262 105.47,250.044 109.129,250.044L120.32,250.044"
    "C121.693,245.175 123.657,240.581 126.121,236.36L118.863,228.559C116.276,225.776 116.276,221.224 118.863,218.444"
    "L128.694,207.874C131.281,205.093 135.514,205.093 138.102,207.874L146.017,216.385"
    "C150.095,214.036 154.491,212.243 159.119,211.104L159.119,200.08C159.119,196.145 162.113,192.926 165.772,192.926"
    "L179.674,192.926C183.333,192.926 186.327,196.145 186.327,200.08L186.327,212.107"
    "C190.857,213.583 195.131,215.693 199.06,218.344L206.313,210.543C208.903,207.762 213.136,207.762 215.723,210.543Z"
    "M170.967,237.173C186.061,237.173 198.299,250.332 198.299,266.558C198.299,282.783 186.061,295.943 170.967,295.943"
    "C155.874,295.943 143.634,282.784 143.634,266.558C143.634,250.332 155.874,237.173 170.967,237.173Z";

// The status bar's symbols, from docs/design/heatunit.svg (their own coordinates, fitted into a box when drawn).
const char* const kSpeakerIcon =
    "M11.32,19.85L33.89,19.85L52.56,1C53.935,-0.364 56.185,-0.364 57.56,1C58.218,1.668 58.578,2.573 58.56,3.51L58.56,81.3"
    "C58.558,83.251 56.951,84.856 55,84.856C54.045,84.856 53.129,84.471 52.46,83.79L34.01,68.79L11.32,68.79"
    "C5.124,68.774 0.027,63.686 0,57.49L0,31.17C0.027,24.97 5.12,19.877 11.32,19.85ZM74.71,31.62"
    "C74.622,31.319 74.578,31.007 74.578,30.693C74.578,28.872 76.077,27.373 77.898,27.373C79.272,27.373 80.51,28.226 81,29.51"
    "C82.14,32.9 82.69,38.17 82.6,43.18C82.51,48.19 81.79,52.9 80.41,55.75C79.853,56.897 78.685,57.629 77.41,57.629"
    "C75.581,57.629 74.076,56.124 74.076,54.295C74.076,53.791 74.19,53.293 74.41,52.84C75.41,50.84 75.88,47.08 75.96,43.07"
    "C76.118,39.211 75.69,35.35 74.69,31.62L74.71,31.62ZM91.85,19.22C91.682,18.816 91.596,18.383 91.596,17.945"
    "C91.596,16.119 93.099,14.616 94.925,14.616C96.268,14.616 97.486,15.429 98,16.67C101.08,24.07 102.75,33.38 102.89,42.67"
    "C103.03,51.96 101.68,60.92 98.75,68.18C98.247,69.433 97.025,70.259 95.675,70.259C93.857,70.259 92.361,68.763 92.361,66.945"
    "C92.361,66.522 92.442,66.103 92.6,65.71C95.2,59.27 96.39,51.04 96.27,42.71C96.15,34.38 94.64,25.85 91.86,19.21"
    "L91.85,19.22ZM108.42,8.68C108.204,8.231 108.092,7.738 108.092,7.24C108.092,5.414 109.594,3.912 111.42,3.912"
    "C112.698,3.912 113.867,4.648 114.42,5.8C119.936,17.546 122.83,30.353 122.9,43.33C123,55.91 120.46,68.45 114.9,79.14"
    "C114.371,80.337 113.181,81.112 111.872,81.112C110.057,81.112 108.562,79.618 108.562,77.802C108.562,77.218 108.717,76.645 109.01,76.14"
    "C114.01,66.43 116.33,54.97 116.24,43.42C116.185,31.422 113.513,19.579 108.41,8.72L108.42,8.68Z";

// Reads the design's path data: absolute M, L, C and Z commands, a letter may be left out when it repeats (pairs
// after M are lines). Stops at anything else.
QPainterPath ParsePath(std::string_view data, Qt::FillRule rule)
{
    QPainterPath path;
    path.setFillRule(rule);
    std::size_t position = 0;
    const auto skipSeparators = [&] {
        while (position < data.size() && (data[position] == ' ' || data[position] == ',')) ++position;
    };
    const auto readNumber = [&](double& value) {
        skipSeparators();
        const std::size_t start = position;
        if (position < data.size() && data[position] == '-') ++position;
        bool hasPoint = false;
        while (position < data.size() && (std::isdigit(static_cast<unsigned char>(data[position])) || (data[position] == '.' && !hasPoint))) {
            hasPoint = hasPoint || data[position] == '.';
            ++position;
        }
        bool isValid = false;
        value = QByteArray(data.data() + start, static_cast<qsizetype>(position - start)).toDouble(&isValid);   // "C" locale
        return isValid;
    };
    char command = 0;
    for (skipSeparators(); position < data.size(); skipSeparators()) {
        if (std::isalpha(static_cast<unsigned char>(data[position]))) command = data[position++];
        else if (command == 'M') command = 'L';
        else if (command == 'Z') break;   // numbers after Z: not path data
        double values[6]{};
        switch (command) {
        case 'M':
        case 'L':
            if (!readNumber(values[0]) || !readNumber(values[1])) return path;
            if (command == 'M') path.moveTo(values[0], values[1]);
            else path.lineTo(values[0], values[1]);
            break;
        case 'C':
            for (double& value : values) {
                if (!readNumber(value)) return path;
            }
            path.cubicTo(values[0], values[1], values[2], values[3], values[4], values[5]);
            break;
        case 'Z':
            path.closeSubpath();
            break;
        default:
            return path;
        }
    }
    return path;
}

// A gradient of the design: 0 to 1 along x, placed by its gradientTransform (in artboard coordinates).
QBrush DesignGradient(const QGradientStops& stops, const QTransform& placement)
{
    QLinearGradient gradient(0, 0, 1, 0);
    gradient.setStops(stops);
    QBrush brush(gradient);
    brush.setTransform(placement);
    return brush;
}

// The stripes' gradient stops: nine colours at the design's offsets.
QGradientStops StripeStops(const std::array<QColor, 9>& colours)
{
    static constexpr double kOffsets[] = {0, 0.06, 0.13, 0.29, 0.5, 0.7, 0.85, 0.93, 1};
    QGradientStops stops;
    for (std::size_t index = 0; index < colours.size(); ++index) stops.append({kOffsets[index], colours[index]});
    return stops;
}

// The shapes and brushes of the tiles. A tile lit by the focus draws its stripes and symbol in orange, the others in grey.
struct TileLook {
    QPainterPath stripeShape;
    std::array<QPainterPath, kHomeMenuCount> iconShapes;
    QBrush stripes, litStripes, icon, litIcon;
};

// The tiles' look, parsed once.
const TileLook& SharedTileLook()
{
    static const TileLook look = [] {
        TileLook result;
        result.stripeShape = ParsePath(kStripes, Qt::OddEvenFill);
        // The design fills with the even-odd rule; only the Android Auto symbol and the microphone use nonzero. In the
        // order of HomeMenuEntry.
        result.iconShapes = {ParsePath(kAndroidAutoIcon, Qt::WindingFill), ParsePath(kMultimediaIcon, Qt::OddEvenFill),
            ParsePath(kRadioIcon, Qt::WindingFill), ParsePath(kTelephoneIcon, Qt::OddEvenFill), ParsePath(kNavigationIcon, Qt::OddEvenFill),
            ParsePath(kVehicleIcon, Qt::OddEvenFill), ParsePath(kSettingsIcon, Qt::OddEvenFill)};
        // Stripes: vertical, brightest in the middle of the tile; grey at half opacity, orange opaque.
        const QTransform vertical(0, 392.557, -392.557, 0, 158.23, 70.6259);
        const auto grey = [](int value) { return QColor(value, value, value, 128); };
        result.stripes = DesignGradient(StripeStops({grey(0), grey(12), grey(41), grey(106), grey(123), grey(66), grey(41), grey(11), grey(0)}), vertical);
        result.litStripes = DesignGradient(StripeStops({QColor(0, 0, 0), QColor(74, 13, 0), QColor(255, 45, 0), QColor(255, 143, 0), QColor(255, 168, 0),
            QColor(255, 83, 0), QColor(255, 45, 0), QColor(68, 12, 0), QColor(0, 0, 0)}), vertical);
        // Symbols: diagonal, darker at the bottom left.
        result.icon = DesignGradient({{0, QColor(23, 23, 23)}, {1, QColor(142, 142, 142)}}, QTransform(203.561, -359.923, 334.786, 218.845, 66.5128, 448.32));
        result.litIcon = DesignGradient({{0, QColor(255, 45, 0)}, {1, QColor(255, 168, 0)}}, QTransform(123.562, -225.494, 209.745, 132.839, 110.546, 365.205));
        return result;
    }();
    return look;
}

// The outline of a player symbol, drawn in a square around the box's centre.
QPainterPath SymbolPath(Symbol symbol, const QRectF& box)
{
    const double size = std::min(box.width(), box.height()) * 0.4;
    const QPointF centre = box.center();
    const double left = centre.x() - size / 2;
    const double top = centre.y() - size / 2;
    const double right = centre.x() + size / 2;
    const double bottom = centre.y() + size / 2;
    QPainterPath path;
    const auto triangle = [&](double from, double to) {   // pointing from `from` towards `to` (x), full height
        path.addPolygon(QPolygonF({QPointF(from, top), QPointF(to, centre.y()), QPointF(from, bottom)}));
        path.closeSubpath();
    };
    const double bar = size * 0.18;
    switch (symbol) {
    case Symbol::Play: triangle(left + size * 0.1, right); break;
    case Symbol::Pause:
        path.addRect(QRectF(left + size * 0.12, top, size * 0.28, size));
        path.addRect(QRectF(right - size * 0.4, top, size * 0.28, size));
        break;
    case Symbol::Stop: path.addRect(QRectF(left + size * 0.08, top + size * 0.08, size * 0.84, size * 0.84)); break;
    case Symbol::Previous:
        path.addRect(QRectF(left, top, bar, size));
        triangle(right, left + bar);
        break;
    case Symbol::Next:
        triangle(left, right - bar);
        path.addRect(QRectF(right - bar, top, bar, size));
        break;
    }
    return path;
}

// The frame of a button: the focus, or a faint frame.
void DrawButtonFrame(QPainter& painter, const QRectF& box, bool isFocused)
{
    if (isFocused) {
        DrawFocus(painter, box);
        return;
    }
    painter.setPen(QPen(kFaintFrame, 2.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);
}
}

// Where a display of `display`'s shape is drawn in an area of `area`'s size: as large as fits and centred, like the
// phone's picture.
QRectF ScreenRectIn(const QSizeF& area, const DisplayConfig& display)
{
    const QSizeF shown = QSizeF(display.width, display.height).scaled(area, Qt::KeepAspectRatio);
    return QRectF(QPointF((area.width() - shown.width()) / 2, (area.height() - shown.height()) / 2), shown);
}

// The design's font (Roboto, else the system's sans serif), `pixels` design units high.
QFont Font(int pixels)
{
    QFont font;
    font.setFamilies({"Roboto", "Segoe UI", "Noto Sans", "DejaVu Sans"});
    font.setPixelSize(pixels);
    return font;
}

// `text` shortened with "..." to fit `width` in `font`.
QString Elided(const QString& text, const QFont& font, double width)
{
    return QFontMetricsF(font).elidedText(text, Qt::ElideRight, std::max(0.0, width));
}

// A tile as on the home menu: stripes, symbol, frame and title. Lit: stripes and symbol in orange.
void DrawTile(QPainter& painter, const QRectF& tile, HomeMenuEntry entry, bool isLit)
{
    const TileLook& look = SharedTileLook();
    painter.save();
    QTransform artboard;
    artboard.translate(tile.left(), tile.top());
    artboard.scale(tile.width() / kArtboard.width(), tile.height() / kArtboard.height());
    artboard.translate(-kArtboard.left(), -kArtboard.top());
    painter.setTransform(artboard, true);
    painter.fillPath(look.stripeShape, isLit ? look.litStripes : look.stripes);
    painter.fillPath(look.iconShapes[static_cast<std::size_t>(entry)], isLit ? look.litIcon : look.icon);
    painter.setPen(QPen(kText, kFrameWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(kArtboard);
    painter.restore();
    painter.setFont(Font(kFontSize));
    painter.setPen(kText);
    painter.drawText(tile.topLeft() + kTitleOffset, QString::fromLatin1(HomeMenuTitle(entry)));
}

// Marks what the controller is on: the tiles' frame with small orange stripes in two corners.
void DrawFocus(QPainter& painter, const QRectF& box)
{
    const QRectF inside = box.adjusted(kFrameWidth / 2, kFrameWidth / 2, -kFrameWidth / 2, -kFrameWidth / 2);
    const double leg = std::min({inside.width(), inside.height(), 56.0}) * 0.6;
    QPainterPath corners;
    corners.addPolygon(QPolygonF({QPointF(inside.right() - leg, inside.top()), inside.topRight(), QPointF(inside.right(), inside.top() + leg)}));
    corners.closeSubpath();
    corners.addPolygon(QPolygonF({QPointF(inside.left(), inside.bottom() - leg), inside.bottomLeft(), QPointF(inside.left() + leg, inside.bottom())}));
    corners.closeSubpath();
    painter.fillPath(corners, LitBrush(box));
    painter.setPen(QPen(kText, kFrameWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);
}

// A framed button like a small tile. `isOn` draws the symbol in orange (the player is playing).
void DrawButton(QPainter& painter, const QRectF& box, Symbol symbol, bool isFocused, bool isOn)
{
    DrawButtonFrame(painter, box, isFocused);
    painter.fillPath(SymbolPath(symbol, box), isOn ? LitBrush(box) : QBrush(kText));
}

// A framed button with a text.
void DrawTextButton(QPainter& painter, const QRectF& box, const QString& text, bool isFocused)
{
    DrawButtonFrame(painter, box, isFocused);
    const QFont font = Font(24);
    painter.setFont(font);
    painter.setPen(kText);
    painter.drawText(box, Qt::AlignCenter, Elided(text, font, box.width() - 20));
}

// A tick box; on: filled with the lit orange and ticked.
void DrawCheck(QPainter& painter, const QRectF& box, bool isOn)
{
    painter.setPen(QPen(isOn ? kText : kFaintFrame, 2.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);
    if (!isOn) return;
    painter.fillRect(box.adjusted(5, 5, -5, -5), LitBrush(box));
    painter.setPen(QPen(Qt::black, 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const QPointF centre = box.center();
    const double size = box.width() * 0.22;
    painter.drawPolyline(QPolygonF({QPointF(centre.x() - size, centre.y()), QPointF(centre.x() - size * 0.25, centre.y() + size * 0.8),
        QPointF(centre.x() + size * 1.1, centre.y() - size * 0.8)}));
}

// One row of a list: text at the left, a dim note at the right. The current row (what plays) has its text in orange.
void DrawRow(QPainter& painter, const QRectF& row, const QString& text, const QString& note, bool isFocused, bool isCurrent)
{
    if (isFocused) {
        DrawFocus(painter, row);
    } else {
        painter.setPen(QPen(kLine, 1.5));
        painter.drawLine(QPointF(row.left(), row.bottom()), QPointF(row.right(), row.bottom()));
    }
    const QFont noteFont = Font(20);
    const double noteWidth = note.isEmpty() ? 0 : std::min(QFontMetricsF(noteFont).horizontalAdvance(note), row.width() * 0.4);
    const QRectF textBox = row.adjusted(18, 0, -(noteWidth + (noteWidth > 0 ? 44 : 18)), 0);
    const QFont font = Font(26);
    painter.setFont(font);
    painter.setPen(isCurrent ? kOrange : kText);
    painter.drawText(textBox, Qt::AlignLeft | Qt::AlignVCenter, Elided(text, font, textBox.width()));
    if (noteWidth > 0) {
        painter.setFont(noteFont);
        painter.setPen(kDim);
        painter.drawText(row.adjusted(0, 0, -26, 0), Qt::AlignRight | Qt::AlignVCenter, Elided(note, noteFont, noteWidth));
    }
}

// A symbol of the status bar, as large as fits into `box` and centred there. Struck (muted): dim, with an orange slash.
void DrawIcon(QPainter& painter, Icon icon, const QRectF& box, bool isStruck)
{
    static const std::array<QPainterPath, 1> kIcons = {ParsePath(kSpeakerIcon, Qt::WindingFill)};
    const QPainterPath& path = kIcons[static_cast<std::size_t>(icon)];
    const QRectF bounds = path.boundingRect();
    if (bounds.isEmpty()) return;
    const double scale = std::min(box.width() / bounds.width(), box.height() / bounds.height());
    const QSizeF size = bounds.size() * scale;
    const QRectF fitted(box.center() - QPointF(size.width() / 2, size.height() / 2), size);
    painter.save();
    painter.translate(fitted.topLeft());
    painter.scale(scale, scale);
    painter.translate(-bounds.topLeft());
    painter.fillPath(path, isStruck ? kDim : kText);
    painter.restore();
    if (!isStruck) return;
    const QLineF slash(fitted.topLeft(), fitted.bottomRight());
    painter.setPen(QPen(Qt::black, 9, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(slash);
    painter.setPen(QPen(LitBrush(fitted), 4.5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(slash);
}

// The lit orange of the design (focus corners, lit segments), diagonally across `box`.
QBrush LitBrush(const QRectF& box)
{
    QLinearGradient gradient(box.bottomLeft(), box.topRight());
    gradient.setColorAt(0, QColor(255, 45, 0));
    gradient.setColorAt(1, QColor(255, 168, 0));
    return gradient;
}
}
