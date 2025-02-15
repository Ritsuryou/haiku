#include <ControlLook.h>
#include <ScrollView.h>
#include <String.h>
#include <TextView.h>
#include <limits.h>

#include "ErrorLogWindow.h"

class Error : public BView {
	public:
		Error(BRect rect, alert_type type, const char *tag, const char *message, bool timestamp,
			rgb_color backgroundColor, rgb_color foregroundColor);

		void GetPreferredSize(float *width, float *height);
		void Draw(BRect updateRect);
		void FrameResized(float w, float h);
	private:
		alert_type type;
};


class ErrorPanel : public BView {
	public:
		ErrorPanel(BRect rect) : BView(rect, "ErrorScrollPanel", B_FOLLOW_ALL_SIDES,
			B_DRAW_ON_CHILDREN | B_FRAME_EVENTS), alerts_displayed(0), add_next_at(0)
		{
			AdoptSystemColors();
		}

		void GetPreferredSize(float *width, float *height) {
			*width = Bounds().Width();
			*height = add_next_at;
		}

		void TargetedByScrollView(BScrollView *scroll_view)
		{
			scroll = scroll_view;
		}

		void FrameResized(float w, float /*h*/) {
			add_next_at = 0;
			for (int32 i = 0; i < CountChildren(); i++) {
				ChildAt(i)->MoveTo(BPoint(0,add_next_at));
				ChildAt(i)->ResizeTo(w, ChildAt(i)->Frame().Height());
				ChildAt(i)->ResizeToPreferred();
				add_next_at += ChildAt(i)->Bounds().Height();
			}
		}
		int32 alerts_displayed;
		float add_next_at;
		BScrollView *scroll;
};


//	#pragma mark -


ErrorLogWindow::ErrorLogWindow(BRect rect, const char *name, window_type type)
	:
	BWindow(rect, name, type, B_NO_WORKSPACE_ACTIVATION | B_NOT_MINIMIZABLE
		| B_ASYNCHRONOUS_CONTROLS),
	fIsRunning(false)
{
	SetColumnColors(ui_color(B_LIST_BACKGROUND_COLOR));
	fTextColor = ui_color(B_LIST_ITEM_TEXT_COLOR);

	rect = Bounds();
	rect.right -= be_control_look->GetScrollBarWidth(B_VERTICAL);

	view = new ErrorPanel(rect);
	AddChild(new BScrollView("ErrorScroller", view, B_FOLLOW_ALL_SIDES, 0, false, true));
	Show();
	Hide();
}


void
ErrorLogWindow::AddError(alert_type type, const char *message, const char *tag, bool timestamp)
{
	ErrorPanel *panel = (ErrorPanel *)view;

	// first call?
	if (!fIsRunning) {
		fIsRunning = true;
		Show();
	}

	Lock();

	Error *newError = new Error(BRect(0, panel->add_next_at, panel->Bounds().right,
		panel->add_next_at + 1), type, tag, message, timestamp,
		(panel->alerts_displayed++ % 2 == 0) ? fColumnColor : fColumnAlternateColor, fTextColor);

	newError->ResizeToPreferred();
	panel->add_next_at += newError->Bounds().Height();
	panel->AddChild(newError);
	panel->ResizeToPreferred();

	if (panel->add_next_at > Frame().Height()) {
		BScrollBar *bar = panel->scroll->ScrollBar(B_VERTICAL);

		bar->SetRange(0, panel->add_next_at - Frame().Height());
		bar->SetSteps(1, Frame().Height());
		bar->SetProportion(Frame().Height() / panel->add_next_at);
	} else
		panel->scroll->ScrollBar(B_VERTICAL)->SetRange(0,0);

	if (IsHidden())
		Show();

	Unlock();
}


void
ErrorLogWindow::MessageReceived(BMessage* message)
{
	rgb_color color;
	if (message->what == B_COLORS_UPDATED) {
		if (message->FindColor(ui_color_name(B_LIST_BACKGROUND_COLOR), &color) == B_OK)
			SetColumnColors(color);

		if (message->FindColor(ui_color_name(B_LIST_ITEM_TEXT_COLOR), &color) == B_OK)
			fTextColor = color;
	}
	BWindow::MessageReceived(message);
}


bool
ErrorLogWindow::QuitRequested()
{
	Hide();

	while (view->CountChildren() != 0) {
		BView* child = view->ChildAt(0);
		view->RemoveChild(child);
		delete child;
	}

	ErrorPanel *panel = (ErrorPanel *)(view);
	panel->add_next_at = 0;
	panel->alerts_displayed = 0;

	view->ResizeToPreferred();
	return false;
}


void
ErrorLogWindow::FrameResized(float newWidth, float newHeight)
{
	ErrorPanel *panel = (ErrorPanel *)view;
	panel->Invalidate();

	if (panel->add_next_at > newHeight) {
		BScrollBar *bar = panel->scroll->ScrollBar(B_VERTICAL);

		bar->SetRange(0, panel->add_next_at - Frame().Height());
		bar->SetSteps(1, Frame().Height());
		bar->SetProportion(Frame().Height() / panel->add_next_at);
	} else
		panel->scroll->ScrollBar(B_VERTICAL)->SetRange(0,0);
}


void
ErrorLogWindow::SetColumnColors(rgb_color color)
{
	fColumnColor = color;
	if (fColumnColor.IsDark())
		fColumnAlternateColor = tint_color(color, 0.85f);
	else
		fColumnAlternateColor = tint_color(color, B_DARKEN_2_TINT);
}


//	#pragma mark -


Error::Error(BRect rect, alert_type atype, const char *tag, const char *message,
	bool timestamp, rgb_color backgroundColor, rgb_color foregroundColor)
	:
	BView(rect,"error", B_FOLLOW_LEFT | B_FOLLOW_RIGHT | B_FOLLOW_TOP,
		B_NAVIGABLE | B_WILL_DRAW | B_FRAME_EVENTS), type(atype)
{
	SetViewColor(backgroundColor);
	SetLowColor(backgroundColor);

	text_run_array array;
	array.count = 1;
	array.runs[0].offset = 0;
	array.runs[0].font = *be_bold_font;
	array.runs[0].color = foregroundColor;

	BString msgString(message);
	msgString.RemoveAll("\r");

	BTextView *view = new BTextView(BRect(20, 0, rect.Width(), rect.Height()),
		"error_display", BRect(0,3,rect.Width() - 20 - 3, LONG_MAX),
		B_FOLLOW_ALL_SIDES);
	view->SetLowColor(backgroundColor);
	view->SetViewColor(backgroundColor);
	view->SetText(msgString.String());
	view->MakeSelectable(true);
	view->SetStylable(true);
	view->MakeEditable(false);

	if (tag != NULL) {
		BString tagString(tag);
		tagString += " ";
		view->Insert(0, tagString.String(), tagString.Length(), &array);
	}

	if (timestamp) {
		array.runs[0].color = foregroundColor.IsLight()
			? tint_color(foregroundColor, B_LIGHTEN_1_TINT)
			: tint_color(foregroundColor, B_DARKEN_2_TINT);
		array.runs[0].font.SetSize(9);
		time_t thetime = time(NULL);
		BString atime = asctime(localtime(&thetime));
		atime.Prepend(" [");
		atime.RemoveAll("\n");
		atime.Append("]");
		view->Insert(view->TextLength(),atime.String(),atime.Length(),&array);
	}

	float height,width;
	width = view->Frame().Width();
	height = view->TextHeight(0,view->CountLines()) + 3;
	view->ResizeTo(width,height);
	AddChild(view);
}


void
Error::GetPreferredSize(float *width, float *height)
{
	BTextView *view = static_cast<BTextView *>(FindView("error_display"));

	*width = view->Frame().Width() + 20;
	*height = view->TextHeight(0, INT32_MAX) + 3;
}


void
Error::Draw(BRect updateRect)
{
	FillRect(updateRect, B_SOLID_LOW);
}


void
Error::FrameResized(float w, float h)
{
	BTextView *view = static_cast<BTextView *>(FindView("error_display"));

	view->ResizeTo(w - 20, h);
	view->SetTextRect(BRect(0, 3, w - 20, h));
}
