#include "common.hpp"

QString processErrorToString(QProcess::ProcessError err)
{
	switch (err) {
	case QProcess::FailedToStart:
		return T_("Process failed to start");
	case QProcess::Crashed:
		return T_("Process crashed");
	default:
		return T_("Unknown error process error");
	}
}
