#include "SlidingStackedWidget.hpp"

SlidingStackedWidget::SlidingStackedWidget(QWidget *parent)
    : QStackedWidget(parent)
{
    if (parent != 0) {
        m_mainwindow = parent;
    }
    else {
        m_mainwindow = this;
        qDebug()<<"ATTENTION: untested mainwindow case !";
    }
    /* Parent should not be 0; not tested for any other case yet !! */
#ifdef Q_OS_SYMBIAN
#ifndef __S60_50__
    qDebug()<< "WARNING: ONLY TESTED AND 5TH EDITION";
#endif /* __S60_50__ */
#endif /* Q_OS_SYMBIAN */
    /* Now, initialize some private variables with default values */
    m_vertical = false;
    //setVerticalMode(true);
    m_speed  = 500;
    m_now    = 0;
    m_next   = 0;
    m_wrap   = false;
    m_pnow   = QPoint(0,0);
    m_active = false;
    m_animationtype = QEasingCurve::OutBack;  /* Check out the QEasingCurve documentation for different styles */
}


SlidingStackedWidget::~SlidingStackedWidget(){
}

void SlidingStackedWidget::setVerticalMode(bool vertical) {
    m_vertical = vertical;
}

void SlidingStackedWidget::setSpeed(int speed) {
    m_speed = speed;
}

void SlidingStackedWidget::setAnimation(enum QEasingCurve::Type animationtype) {
    m_animationtype = animationtype;
}

void SlidingStackedWidget::setWrap(bool wrap) {
    m_wrap = wrap;
}

void SlidingStackedWidget::slideInNext() {
    int now = currentIndex();
    if (m_wrap || (now<count() - 1))
        /* Count is inherit from QStackedWidget */
        slideInIdx(now + 1);
}


void SlidingStackedWidget::slideInPrev() {
    int now = currentIndex();
    if (m_wrap || (now > 0))
        slideInIdx(now - 1);
}

void SlidingStackedWidget::slideInIdx(int idx, enum t_direction direction) {
    //int idx, t_direction direction = AUTOMATIC;
    if (idx > count() - 1) {
        /* Here is an '=', indeed. */
        direction = m_vertical ? TOP2BOTTOM : RIGHT2LEFT;
        idx = idx % count();
    }
    else if (idx < 0) {
        direction = m_vertical ? BOTTOM2TOP: LEFT2RIGHT;
        idx = (idx + count()) % count();
    }
    slideInWgt(widget(idx), direction);
    /* widget() is a function inherited from QStackedWidget */
}


void SlidingStackedWidget::slideInWgt(QWidget *newwidget, enum t_direction direction) {
    if (m_active) {
        return;
        /* At the moment, do not allow re-entrance before an animation is completed.
         * other possibility may be to finish the previous animation abrupt, or
         * to revert the previous animation with a counter animation, before going ahead
         * or to revert the previous animation abrupt
         * and all those only, if the newwidget is not the same as that of the previous running animation.
         */
    }
    else {
        m_active = true;
    }

    enum t_direction directionhint;
    int now  = currentIndex(); /* currentIndex() is a function inherited from QStackedWidget */
    int next = indexOf(newwidget);
    if (now == next) {
        m_active = false;
        return;
    }
    else if (now < next){
        /* Here is a '=', indeed. */
        directionhint = m_vertical ? TOP2BOTTOM : RIGHT2LEFT;
    }
    else {
        directionhint = m_vertical ? BOTTOM2TOP : LEFT2RIGHT;
    }
    if (direction == AUTOMATIC) {
        direction = directionhint;
    }
    /* NOW....calculate the shifts */
    int offsetx = frameRect().width();  /* Inherited from mother */
    int offsety = frameRect().height(); /* Inherited from mother */

    /* The following is important, to ensure that the new widget
       has correct geometry information when sliding in first time */
    widget(next)->setGeometry (0, 0, offsetx, offsety);

    if (direction == BOTTOM2TOP)  {
        offsetx = 0;
        offsety = -offsety;
    }
    else if (direction == TOP2BOTTOM) {
        offsetx = 0;
        //offsety = offsety;
    }
    else if (direction == RIGHT2LEFT) {
        offsetx = -offsetx;
        offsety = 0;
    }
    else if (direction == LEFT2RIGHT) {
        //offsetx = offsetx;
        offsety = 0;
    }
    /* Re-position the next widget outside/aside of the display area */
    QPoint pnext = widget(next)->pos();
    QPoint pnow  = widget(now)->pos();
    m_pnow = pnow;

    widget(next)->move(pnext.x() - offsetx, pnext.y() - offsety);
    /* Make it visible/show */
    widget(next)->show();
    widget(next)->raise();

    /* Animate both, the now and next widget to the side, using animation framework */
    QPropertyAnimation *animnow = new QPropertyAnimation(widget(now), "pos");

    animnow->setDuration(m_speed);
    animnow->setEasingCurve(m_animationtype);
    animnow->setStartValue(QPoint(pnow.x(), pnow.y()));
    animnow->setEndValue(QPoint(offsetx + pnow.x(), offsety + pnow.y()));
    QPropertyAnimation *animnext = new QPropertyAnimation(widget(next), "pos");
    animnext->setDuration(m_speed);
    animnext->setEasingCurve(m_animationtype);
    animnext->setStartValue(QPoint(-offsetx + pnext.x(), offsety + pnext.y()));
    animnext->setEndValue(QPoint(pnext.x(), pnext.y()));

    QParallelAnimationGroup *animgroup = new QParallelAnimationGroup;

    animgroup->addAnimation(animnow);
    animgroup->addAnimation(animnext);

    QObject::connect(animgroup, SIGNAL(finished()), this, SLOT(animationDoneSlot()));
    m_next   = next;
    m_now    = now;
    m_active = true;
    animgroup->start();

    /* Note:
     * the rest is done via a connect from the animation ready;
     * animation->finished() provides a signal when animation is done;
     * so we connect this to some post processing slot,
     * that we implement here below in animationDoneSlot.
     */
}


void SlidingStackedWidget::animationDoneSlot(void) {
    /* When ready, call the QStackedWidget slot setCurrentIndex(int) */
    setCurrentIndex(m_next);  /* This function is inherit from QStackedWidget */
    /* Then hide the outshifted widget now, and  (may be done already implicitely by QStackedWidget) */
    widget(m_now)->hide();
    /* Then set the position of the outshifted widget now back to its original */
    widget(m_now)->move(m_pnow);
    /* So that the application could also still call the QStackedWidget original functions/slots for changings */
    //widget(m_now)->update();
    //setCurrentIndex(m_next);  /* This function is inherit from QStackedWidget */
    m_active = false;
    emit animationFinished();
}

/* REFERENCES:
 * http://doc.trolltech.com/4.6/animation-overview.html#easing-curves
 * http://doc.trolltech.com/4.6/qpropertyanimation.html
 * http://doc.trolltech.com/4.6/qanimationgroup.html
 */
