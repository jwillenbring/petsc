
/*
       Provides the calling sequences for all the basic PetscDraw routines.
*/
#include <petsc/private/drawimpl.h> /*I "petscdraw.h" I*/

/*@
    PetscDrawGetMouseButton - Returns location of mouse and which button was
    pressed. Waits for button to be pressed.

    Collective

    Input Parameter:
.   draw - the window to be used

    Output Parameters:
+   button - one of `PETSC_BUTTON_LEFT`, `PETSC_BUTTON_CENTER`, `PETSC_BUTTON_RIGHT`, `PETSC_BUTTON_WHEEL_UP`, `PETSC_BUTTON_WHEEL_DOWN`
.   x_user - horizontal user coordinate of location (user may pass in NULL).
.   y_user - vertical user coordinate of location (user may pass in NULL).
.   x_phys - horizontal window coordinate (user may pass in NULL).
-   y_phys - vertical window coordinate (user may pass in NULL).

    Note:
    Only processor 0 actually waits for the button to be pressed.

    Level: intermediate

.seealso: `PetscDraw`, `PetscDrawButton`
@*/
PetscErrorCode PetscDrawGetMouseButton(PetscDraw draw, PetscDrawButton *button, PetscReal *x_user, PetscReal *y_user, PetscReal *x_phys, PetscReal *y_phys)
{
  PetscReal bcast[4] = {0, 0, 0, 0};

  PetscFunctionBegin;
  PetscValidHeaderSpecific(draw, PETSC_DRAW_CLASSID, 1);
  PetscValidPointer(button, 2);
  *button = PETSC_BUTTON_NONE;
  if (!draw->ops->getmousebutton) PetscFunctionReturn(PETSC_SUCCESS);

  PetscUseTypeMethod(draw, getmousebutton, button, x_user, y_user, x_phys, y_phys);

  PetscCallMPI(MPI_Bcast((PetscEnum *)button, 1, MPIU_ENUM, 0, PetscObjectComm((PetscObject)draw)));
  if (x_user) bcast[0] = *x_user;
  if (y_user) bcast[1] = *y_user;
  if (x_phys) bcast[2] = *x_phys;
  if (y_phys) bcast[3] = *y_phys;
  PetscCallMPI(MPI_Bcast(bcast, 4, MPIU_REAL, 0, PetscObjectComm((PetscObject)draw)));
  if (x_user) *x_user = bcast[0];
  if (y_user) *y_user = bcast[1];
  if (x_phys) *x_phys = bcast[2];
  if (y_phys) *y_phys = bcast[3];
  PetscFunctionReturn(PETSC_SUCCESS);
}
